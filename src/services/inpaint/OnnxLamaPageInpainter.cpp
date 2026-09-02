#include "OnnxLamaPageInpainter.h"

#include "application/operation/AsyncResult.h"
#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/LamaMaskBuilder.h"
#include "services/inpaint/LamaTilePlanner.h"

#include <QColorSpace>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>

#include <algorithm>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <optional>
#include <vector>

namespace manga::services {
namespace {
using application::ApplicationError;
using application::Result;

constexpr float kInv255 = 1.0f / 255.0f;

ApplicationError inputError(const QString& code, const QString& text) {
    return application::makeApplicationError(application::ErrorCategory::Input, code, text);
}
ApplicationError modelError(const QString& code, const QString& text) {
    return application::makeApplicationError(application::ErrorCategory::Model, code, text);
}
ApplicationError inferenceError(const QString& code, const QString& text) {
    return application::makeApplicationError(
        application::ErrorCategory::Inference, code, text, application::RecoveryAction::Retry);
}
ApplicationError cancelled() {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.user_requested"),
                                             QStringLiteral("The inpainting operation was cancelled"));
}
Result<void> checkpoint(const application::OperationContext& context) {
    if (context.stopToken.stop_requested())
        return std::unexpected(cancelled());
    return {};
}

application::ClearResponse
maskedClear(const application::ClearRequest& request, const LamaClearMask& clearMask, const QColor& color) {
    application::ClearResponse response;
    response.regionId = request.regionId;
    const QRect& bounds = clearMask.dirtyBounds;
    response.affectedBounds = domain::RectGeometry{static_cast<double>(bounds.x()),
                                                   static_cast<double>(bounds.y()),
                                                   static_cast<double>(bounds.width()),
                                                   static_cast<double>(bounds.height())};
    response.patch = makeColorPatch(bounds, color, clearMask.mask);
    return response;
}

} // namespace

struct OnnxLamaPageInpainter::Impl {
    Ort::Env* env{};
    BoundedTaskExecutor* executor{};
    InpaintManifest manifest;
    std::unique_ptr<Ort::Session> session;
    bool loaded{false};
    QMutex loadMutex;
    QMutex runMutex;
};

OnnxLamaPageInpainter::OnnxLamaPageInpainter(Ort::Env* env, BoundedTaskExecutor* executor, InpaintManifest manifest)
    : m_impl(std::make_shared<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->manifest = std::move(manifest);
}

OnnxLamaPageInpainter::~OnnxLamaPageInpainter() = default;

void OnnxLamaPageInpainter::release() {
    QMutexLocker locker(&m_impl->loadMutex);
    QMutexLocker runLocker(&m_impl->runMutex);
    if (m_impl->loaded) {
        m_impl->session.reset();
        m_impl->loaded = false;
        qInfo().noquote() << "[InpaintEngine] inpaint ONNX session released from memory";
    }
}

application::Result<void> OnnxLamaPageInpainter::ensureLoaded() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->loaded) {
        return {};
    }

    if (m_impl->env == nullptr) {
        return std::unexpected(modelError(QStringLiteral("model.load_failed"),
                                          QStringLiteral("The inpainting model could not be loaded")));
    }

    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const int threadCount = std::clamp(static_cast<int>(hardwareThreads), 2, 8);
    options.SetIntraOpNumThreads(threadCount);
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    qInfo().noquote() << "[InpaintEngine] loading inpaint model from dir=" << m_impl->manifest.modelDirectory
                      << "file=" << m_impl->manifest.modelFile;
    try {
#ifdef _WIN32
        const auto path = QDir(m_impl->manifest.modelDirectory).filePath(m_impl->manifest.modelFile).toStdWString();
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, path.c_str(), options);
#else
        const auto path = QDir(m_impl->manifest.modelDirectory).filePath(m_impl->manifest.modelFile).toStdString();
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, path.c_str(), options);
#endif
        qInfo().noquote() << "[InpaintEngine] inpaint ONNX session loaded successfully";
    } catch (const std::exception& ex) {
        qWarning().noquote() << "[InpaintEngine] failed to create inpaint session:" << ex.what();
        auto error = application::makeApplicationError(application::ErrorCategory::Model,
                                                       QStringLiteral("model.corrupt"),
                                                       QStringLiteral("Failed to load the inpainting ONNX session"),
                                                       application::RecoveryAction::LocateModel);
        error.diagnostic = QString::fromUtf8(ex.what());
        return std::unexpected(error);
    } catch (...) {
        qWarning().noquote() << "[InpaintEngine] unknown failure creating inpaint session";
        return std::unexpected(
            modelError(QStringLiteral("model.corrupt"), QStringLiteral("Failed to load the inpainting ONNX session")));
    }
    m_impl->loaded = true;
    return {};
}

QFuture<Result<application::PageInpaintResponse>>
OnnxLamaPageInpainter::inpaint(application::PageInpaintRequest request, application::OperationContext context) {
    if (!m_impl || !m_impl->executor)
        return application::makeFailedFuture<application::PageInpaintResponse>(
            inputError(QStringLiteral("inference.unavailable"), QStringLiteral("Inpainting is unavailable")));
    auto impl = m_impl;
    auto submitted = impl->executor->submit<application::PageInpaintResponse>(
        application::OperationStage::Clear,
        TaskQueue::Interactive,
        context,
        [this, impl, request = std::move(request)](
            const application::OperationContext& ctx,
            application::TaskProgress& progress) -> Result<application::PageInpaintResponse> {
            if (auto r = checkpoint(ctx); !r)
                return std::unexpected(r.error());
            if (request.sourceImage.isNull() || request.mask.isNull() ||
                request.sourceImage.size() != request.mask.size())
                return std::unexpected(inputError(QStringLiteral("inference.invalid_input"),
                                                  QStringLiteral("Source and mask must have identical dimensions")));
            const QSize size = request.sourceImage.size();
            if (request.dirtyBounds.isEmpty() || !QRect(QPoint(0, 0), size).contains(request.dirtyBounds))
                return std::unexpected(inputError(QStringLiteral("inference.invalid_dirty_bounds"),
                                                  QStringLiteral("Dirty bounds are empty or outside the image")));

            const QImage source = request.sourceImage.convertToFormat(QImage::Format_RGBA8888);
            const QImage mask = request.mask.convertToFormat(QImage::Format_Grayscale8);
            if (source.isNull() || mask.isNull())
                return std::unexpected(
                    inputError(QStringLiteral("inference.invalid_input"),
                               QStringLiteral("Source and mask could not be converted to the required formats")));

            const QRect dirty = request.dirtyBounds;
            const QImage dirtyMask = mask.copy(dirty);
            // Planning binarises at 127 while the network binarises at non-zero, so antialiased rims
            // would fall outside every core. Promote them to keep planning and writeback in agreement.
            QImage planningMask = dirtyMask;
            for (int y = 0; y < planningMask.height(); ++y) {
                auto* row = planningMask.scanLine(y);
                for (int x = 0; x < planningMask.width(); ++x)
                    if (row[x])
                        row[x] = 255;
            }
            const std::vector<LamaTile> tiles = planLamaTiles(planningMask,
                                                              QRect(QPoint(0, 0), planningMask.size()),
                                                              impl->manifest.tileCore,
                                                              impl->manifest.tileContext);
            QImage result = source;
            result.setColorSpace(QColorSpace::SRgb);
            if (tiles.empty()) {
                application::PageInpaintResponse response{request.projectId, request.pageId, std::move(result), dirty};
                return response;
            }

            progress.setText(QStringLiteral("Loading inpainting model..."));
            if (auto r = ensureLoaded(); !r)
                return std::unexpected(r.error());
            if (!impl->session)
                return std::unexpected(modelError(QStringLiteral("model.load_failed"),
                                                  QStringLiteral("The inpainting model could not be loaded")));

            QElapsedTimer timer;
            timer.start();
            qInfo().noquote() << "[InpaintEngine] start inpainting page="
                              << QString::fromStdString(request.pageId.toString()) << "imgSize=" << size
                              << "dirtyBounds=" << dirty << "tiles=" << int(tiles.size());

            progress.setRange(0, int(tiles.size()));
            progress.setValue(0);
            const int padMultiple = std::max(1, impl->manifest.padMultiple);
            Ort::AllocatorWithDefaultOptions allocator;
            const size_t inputCount = impl->session->GetInputCount();
            const size_t outputCount = impl->session->GetOutputCount();
            if (inputCount != 2 || outputCount < 1)
                return std::unexpected(modelError(
                    QStringLiteral("model.incompatible"),
                    QStringLiteral("Inpainting model must expose RGB and mask inputs and one RGB output tensor")));
            std::vector<Ort::AllocatedStringPtr> names;
            for (size_t i = 0; i < inputCount; ++i)
                names.push_back(impl->session->GetInputNameAllocated(i, allocator));
            size_t imageInput = inputCount, maskInput = inputCount;
            for (size_t i = 0; i < inputCount; ++i) {
                const QString name = QString::fromUtf8(names[i].get());
                if (name == impl->manifest.inputName)
                    imageInput = i;
                if (name == impl->manifest.maskInputName)
                    maskInput = i;
            }
            std::optional<Ort::AllocatedStringPtr> outputName;
            for (size_t i = 0; i < outputCount; ++i) {
                auto candidate = impl->session->GetOutputNameAllocated(i, allocator);
                if (QString::fromUtf8(candidate.get()) == impl->manifest.outputName) {
                    outputName = std::move(candidate);
                    break;
                }
            }
            if (imageInput == inputCount || maskInput == inputCount || imageInput == maskInput ||
                !outputName.has_value())
                return std::unexpected(
                    modelError(QStringLiteral("model.incompatible"),
                               QStringLiteral("Manifest tensor names do not match the runtime model")));

            Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            const QPoint offset = dirty.topLeft();
            for (size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                if (auto r = checkpoint(ctx); !r)
                    return std::unexpected(r.error());
                const LamaTile& tile = tiles[tileIndex];
                const QRect corePage = tile.core.translated(offset);
                const QRect cropPage = tile.crop.translated(offset);
                std::vector<int> xIndices, yIndices;
                buildSymmetricPadIndices(cropPage.width(), padMultiple, xIndices);
                buildSymmetricPadIndices(cropPage.height(), padMultiple, yIndices);
                const int width = int(xIndices.size());
                const int height = int(yIndices.size());
                const size_t plane = size_t(width) * height;
                std::vector<float> rgb(size_t(3) * plane, 0.0f), binary(plane, 0.0f);
                for (int py = 0; py < height; ++py) {
                    const int sy = cropPage.top() + yIndices[py];
                    const auto* sp = source.constScanLine(sy);
                    const auto* mp = mask.constScanLine(sy);
                    for (int px = 0; px < width; ++px) {
                        const int sx = cropPage.left() + xIndices[px];
                        const auto* q = sp + sx * 4;
                        const size_t p = size_t(py) * width + px;
                        rgb[p] = q[0] * kInv255;
                        rgb[plane + p] = q[1] * kInv255;
                        rgb[2 * plane + p] = q[2] * kInv255;
                        binary[p] = mp[sx] ? 1.0f : 0.0f;
                    }
                }
                const int64_t shapeRgb[] = {1, 3, height, width};
                const int64_t shapeMask[] = {1, 1, height, width};
                Ort::Value tensors[2] = {
                    Ort::Value::CreateTensor<float>(memory, rgb.data(), rgb.size(), shapeRgb, 4),
                    Ort::Value::CreateTensor<float>(memory, binary.data(), binary.size(), shapeMask, 4)};
                const char* inputNames[] = {names[imageInput].get(), names[maskInput].get()};
                const char* outputNames[] = {outputName->get()};
                if (auto r = checkpoint(ctx); !r)
                    return std::unexpected(r.error());
                std::vector<Ort::Value> outputs;
                try {
                    QMutexLocker lock(&impl->runMutex);
                    outputs = impl->session->Run(Ort::RunOptions{nullptr}, inputNames, tensors, 2, outputNames, 1);
                } catch (const Ort::Exception&) {
                    return std::unexpected(inferenceError(QStringLiteral("inference.failed"),
                                                          QStringLiteral("ONNX Runtime inference failed")));
                }
                if (auto r = checkpoint(ctx); !r)
                    return std::unexpected(r.error());
                if (outputs.size() != 1 || !outputs[0].IsTensor())
                    return std::unexpected(modelError(QStringLiteral("model.incompatible"),
                                                      QStringLiteral("Inpainting output is not a tensor")));
                auto info = outputs[0].GetTensorTypeAndShapeInfo();
                const auto outShape = info.GetShape();
                if (outShape.size() != 4 || outShape[0] != 1 || outShape[1] != 3 || outShape[2] != height ||
                    outShape[3] != width)
                    return std::unexpected(
                        modelError(QStringLiteral("model.incompatible"),
                                   QStringLiteral("Inpainting output shape does not match the tile")));
                const float* values = outputs[0].GetTensorData<float>();
                // The generated tile is blended over the original with the soft mask as alpha so
                // antialiased glyph edges transition instead of showing a hard cut. Writing is clipped
                // to the crop because a split tile keeps the grid cell as its core while its crop only
                // follows the component's tight extent.
                const QRect writeRect = corePage.intersected(cropPage);
                for (int sy = writeRect.top(); sy <= writeRect.bottom(); ++sy) {
                    const auto* sp = source.constScanLine(sy);
                    const auto* mp = mask.constScanLine(sy);
                    auto* row = result.scanLine(sy);
                    for (int sx = writeRect.left(); sx <= writeRect.right(); ++sx) {
                        const uint8_t mv = mp[sx];
                        if (!mv)
                            continue;
                        const size_t p = size_t(sy - cropPage.top()) * width + size_t(sx - cropPage.left());
                        const float alpha = mv * kInv255;
                        const float keep = 1.0f - alpha;
                        auto* d = row + sx * 4;
                        for (int c = 0; c < 3; ++c) {
                            const float generated = std::clamp(values[c * plane + p], 0.0f, 1.0f) * 255.0f;
                            d[c] = static_cast<uchar>(sp[sx * 4 + c] * keep + generated * alpha + 0.5f);
                        }
                    }
                }
                progress.setValue(int(tileIndex) + 1);
            }
            qInfo().noquote() << "[InpaintEngine] inpainting completed in" << timer.elapsed() << "ms";
            application::PageInpaintResponse response{request.projectId, request.pageId, std::move(result), dirty};
            return response;
        });
    if (!submitted)
        return application::makeFailedFuture<application::PageInpaintResponse>(submitted.error());
    return *submitted;
}

QFuture<application::Result<application::ClearResponse>>
OnnxLamaPageInpainter::clear(application::ClearRequest request, application::OperationContext context) {
    if (request.pageImage.isNull() || !std::isfinite(request.region.x) || !std::isfinite(request.region.y) ||
        !std::isfinite(request.region.width) || !std::isfinite(request.region.height)) {
        return application::makeFailedFuture<application::ClearResponse>(
            inputError(QStringLiteral("inference.invalid_input"), QStringLiteral("Invalid clear request geometry")));
    }

    const QSize size = request.pageImage.size();
    if (size.isEmpty()) {
        return application::makeFailedFuture<application::ClearResponse>(
            inputError(QStringLiteral("inference.invalid_input"), QStringLiteral("Page image is empty")));
    }

    const int rx = qFloor(request.region.x);
    const int ry = qFloor(request.region.y);
    const int rw = std::max(1, qCeil(request.region.width));
    const int rh = std::max(1, qCeil(request.region.height));
    const QRect regionRect(rx, ry, rw, rh);
    const int contextMargin = m_impl ? m_impl->manifest.tileContext : 128;
    const QImage page = request.pageImage.format() == QImage::Format_RGBA8888
                            ? request.pageImage
                            : request.pageImage.convertToFormat(QImage::Format_RGBA8888);
    const LamaClearMask clearMask = buildClearMask(page, regionRect, request.polygon, size, contextMargin);
    if (clearMask.dirtyBounds.isEmpty() || clearMask.mask.isNull()) {
        return application::makeFailedFuture<application::ClearResponse>(
            inputError(QStringLiteral("inference.invalid_input"), QStringLiteral("Region is outside image bounds")));
    }

    if (!m_impl || !m_impl->executor)
        return application::makeReadyFuture<application::ClearResponse>(maskedClear(request, clearMask, Qt::white));

    // A bubble whose remaining background is flat is filled with that background instead of being
    // sent to LaMa, which keeps the original bubble colour rather than a reconstruction of it.
    if (auto uniformColor = uniformBackgroundColor(page, clearMask, request.polygon)) {
        qInfo().noquote() << "[InpaintEngine] detected uniform background color=" << uniformColor->name()
                          << "for regionId=" << QString::fromStdString(request.regionId.toString())
                          << "- performing fast uniform fill";
        return application::makeReadyFuture<application::ClearResponse>(maskedClear(request, clearMask, *uniformColor));
    }

    application::PageInpaintRequest inpaintReq;
    inpaintReq.projectId = request.projectId;
    inpaintReq.pageId = request.pageId;
    inpaintReq.sourceImage = page.copy(clearMask.dirtyBounds);
    inpaintReq.mask = clearMask.mask;
    inpaintReq.dirtyBounds = QRect(QPoint(0, 0), clearMask.mask.size());

    auto promise = std::make_shared<QPromise<Result<application::ClearResponse>>>();
    promise->start();
    auto future = promise->future();

    inpaint(std::move(inpaintReq), context)
        .then([promise, request, clearMask](Result<application::PageInpaintResponse> res) {
            if (!res) {
                if (res.error().category == application::ErrorCategory::Cancelled) {
                    promise->addResult(std::unexpected(res.error()));
                } else {
                    qWarning().noquote() << "[InpaintEngine] LaMa inpaint failed, falling back to solid fill:"
                                         << res.error().safeMessage;
                    promise->addResult(maskedClear(request, clearMask, Qt::white));
                }
            } else {
                application::ClearResponse response;
                response.regionId = request.regionId;
                const QRect& bounds = clearMask.dirtyBounds;
                response.affectedBounds = domain::RectGeometry{static_cast<double>(bounds.x()),
                                                               static_cast<double>(bounds.y()),
                                                               static_cast<double>(bounds.width()),
                                                               static_cast<double>(bounds.height())};
                response.patch = makeInpaintedPatch(bounds, res->image, clearMask.mask);
                promise->addResult(response);
            }
            promise->finish();
        });

    return future;
}

} // namespace manga::services
