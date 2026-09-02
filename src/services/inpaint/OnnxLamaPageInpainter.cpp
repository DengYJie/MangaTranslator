#include "OnnxLamaPageInpainter.h"

#include "application/operation/AsyncResult.h"
#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QColorSpace>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPolygonF>
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
            progress.setText(QStringLiteral("Loading inpainting model..."));
            if (auto r = ensureLoaded(); !r)
                return std::unexpected(r.error());
            if (request.sourceImage.isNull() || request.mask.isNull() ||
                request.sourceImage.size() != request.mask.size())
                return std::unexpected(inputError(QStringLiteral("inference.invalid_input"),
                                                  QStringLiteral("Source and mask must have identical dimensions")));
            const QSize size = request.sourceImage.size();
            if (request.dirtyBounds.isEmpty() || !QRect(QPoint(0, 0), size).contains(request.dirtyBounds))
                return std::unexpected(inputError(QStringLiteral("inference.invalid_dirty_bounds"),
                                                  QStringLiteral("Dirty bounds are empty or outside the image")));
            if (!impl->session)
                return std::unexpected(modelError(QStringLiteral("model.load_failed"),
                                                  QStringLiteral("The inpainting model could not be loaded")));

            QElapsedTimer timer;
            timer.start();
            qInfo().noquote() << "[InpaintEngine] start inpainting page="
                              << QString::fromStdString(request.pageId.toString()) << "imgSize=" << size
                              << "dirtyBounds=" << request.dirtyBounds;

            const QImage source = request.sourceImage.convertToFormat(QImage::Format_RGBA8888);
            const QImage mask = request.mask.convertToFormat(QImage::Format_Grayscale8);
            if (source.isNull() || mask.isNull())
                return std::unexpected(
                    inputError(QStringLiteral("inference.invalid_input"),
                               QStringLiteral("Source and mask could not be converted to the required formats")));
            QImage result = source;
            result.setColorSpace(QColorSpace::SRgb);
            const QRect imageRect(QPoint(0, 0), size);
            const int core = impl->manifest.tileCore;
            const int contextPixels = impl->manifest.tileContext;
            const QRect dirty = request.dirtyBounds;
            const int nx = (dirty.width() + core - 1) / core;
            const int ny = (dirty.height() + core - 1) / core;
            progress.setRange(0, nx * ny);
            progress.setValue(0);
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
            size_t outputIndex = outputCount;
            std::optional<Ort::AllocatedStringPtr> outputName;
            for (size_t i = 0; i < outputCount; ++i) {
                auto candidate = impl->session->GetOutputNameAllocated(i, allocator);
                if (QString::fromUtf8(candidate.get()) == impl->manifest.outputName) {
                    outputIndex = i;
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
            for (int yTile = 0; yTile < ny; ++yTile)
                for (int xTile = 0; xTile < nx; ++xTile) {
                    if (auto r = checkpoint(ctx); !r)
                        return std::unexpected(r.error());
                    const QRect tileCore(dirty.left() + xTile * core,
                                         dirty.top() + yTile * core,
                                         std::min(core, dirty.right() - (dirty.left() + xTile * core) + 1),
                                         std::min(core, dirty.bottom() - (dirty.top() + yTile * core) + 1));
                    const QRect tile = tileCore.adjusted(-contextPixels, -contextPixels, contextPixels, contextPixels)
                                           .intersected(imageRect);
                    const int width = (tile.width() + 7) / 8 * 8;
                    const int height = (tile.height() + 7) / 8 * 8;
                    std::vector<float> rgb(size_t(3) * width * height, 0.0f), binary(size_t(width) * height, 0.0f);
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x) {
                            const int sx = std::clamp(x, 0, tile.width() - 1) + tile.left();
                            const int sy = std::clamp(y, 0, tile.height() - 1) + tile.top();
                            const auto* sp = source.constScanLine(sy) + sx * 4;
                            const auto* mp = mask.constScanLine(sy) + sx;
                            const size_t p = size_t(y) * width + x;
                            rgb[p] = sp[0] / 255.0f;
                            rgb[size_t(width) * height + p] = sp[1] / 255.0f;
                            rgb[2 * size_t(width) * height + p] = sp[2] / 255.0f;
                            binary[p] = *mp ? 1.0f : 0.0f;
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
                    const size_t plane = size_t(width) * height;
                    for (int y = 0; y < tileCore.height(); ++y)
                        for (int x = 0; x < tileCore.width(); ++x) {
                            const int sx = tileCore.left() + x, sy = tileCore.top() + y;
                            if (!mask.constScanLine(sy)[sx])
                                continue;
                            const int ox = sx - tile.left(), oy = sy - tile.top();
                            const size_t p = size_t(oy) * width + ox;
                            auto* dp = result.scanLine(sy) + sx * 4;
                            dp[0] = static_cast<uchar>(std::clamp(values[p], 0.0f, 1.0f) * 255.0f + 0.5f);
                            dp[1] = static_cast<uchar>(std::clamp(values[plane + p], 0.0f, 1.0f) * 255.0f + 0.5f);
                            dp[2] = static_cast<uchar>(std::clamp(values[2 * plane + p], 0.0f, 1.0f) * 255.0f + 0.5f);
                        }
                    progress.setValue(yTile * nx + xTile + 1);
                }
            qInfo().noquote() << "[InpaintEngine] inpainting completed in" << timer.elapsed() << "ms";
            application::PageInpaintResponse response{
                request.projectId, request.pageId, std::move(result), request.dirtyBounds};
            return response;
        });
    if (!submitted)
        return application::makeFailedFuture<application::PageInpaintResponse>(submitted.error());
    return *submitted;
}

namespace {

uint8_t calcMedian(std::vector<uint8_t>& values) {
    if (values.empty()) {
        return 255;
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values[mid];
}

double calcStdDev(const std::vector<uint8_t>& values, double medianVal) {
    if (values.empty()) {
        return 0.0;
    }
    double sumSq = 0.0;
    for (uint8_t v : values) {
        const double diff = static_cast<double>(v) - medianVal;
        sumSq += diff * diff;
    }
    return std::sqrt(sumSq / static_cast<double>(values.size()));
}

// Aligns with Koharu's uniform_region_color algorithm (inpainting.rs:774-814)
std::optional<QColor> detectUniformBackgroundColor(const QImage& image,
                                                   const QRect& regionRect,
                                                   const std::optional<domain::PolygonGeometry>& polygon) {
    Q_UNUSED(polygon);
    const int imgW = image.width();
    const int imgH = image.height();
    const QImage rgba = (image.format() == QImage::Format_RGBA8888 || image.format() == QImage::Format_RGB32 ||
                         image.format() == QImage::Format_ARGB32)
                            ? image
                            : image.convertToFormat(QImage::Format_RGBA8888);

    std::vector<uint8_t> rChan, gChan, bChan;
    rChan.reserve(1024);
    gChan.reserve(1024);
    bChan.reserve(1024);

    // Sample background reference pixels around the border band of the text region (outer 4-8px)
    // and inside the region away from dark text strokes.
    const int pad = 6;
    const QRect sampleRect = regionRect.adjusted(-pad, -pad, pad, pad).intersected(QRect(0, 0, imgW, imgH));

    for (int y = sampleRect.top(); y <= sampleRect.bottom(); ++y) {
        const auto* scan = rgba.constScanLine(y);
        for (int x = sampleRect.left(); x <= sampleRect.right(); ++x) {
            const int offset = x * 4;
            const uint8_t r = scan[offset + 0];
            const uint8_t g = scan[offset + 1];
            const uint8_t b = scan[offset + 2];

            const double luma = 0.299 * r + 0.587 * g + 0.114 * b;

            // Filter out dark text strokes (luma < 120) so they don't spoil background variance
            if (luma >= 120.0) {
                rChan.push_back(r);
                gChan.push_back(g);
                bChan.push_back(b);
            }
        }
    }

    if (rChan.size() < 16) {
        return std::nullopt;
    }

    const uint8_t medR = calcMedian(rChan);
    const uint8_t medG = calcMedian(gChan);
    const uint8_t medB = calcMedian(bChan);

    const double devR = calcStdDev(rChan, medR);
    const double devG = calcStdDev(gChan, medG);
    const double devB = calcStdDev(bChan, medB);

    const double meanDev = (devR + devG + devB) / 3.0;
    const double devSpread = std::sqrt(((devR - meanDev) * (devR - meanDev) + (devG - meanDev) * (devG - meanDev) +
                                        (devB - meanDev) * (devB - meanDev)) /
                                       3.0);

    const double threshold = (devSpread > 1.0) ? 7.0 : 10.0;
    const double maxDev = std::max({devR, devG, devB});

    // If max deviation is within threshold, it is a uniform flat background!
    // Or if all channels are bright near white (> 220 with dev < 15.0)
    if (maxDev < threshold || (medR > 220 && medG > 220 && medB > 220 && maxDev < 15.0)) {
        return QColor(medR, medG, medB);
    }

    return std::nullopt;
}

application::ClearResponse uniformClear(const application::ClearRequest& request, const QColor& color) {
    const int left = qFloor(request.region.x);
    const int top = qFloor(request.region.y);
    const int right = qCeil(request.region.x + request.region.width);
    const int bottom = qCeil(request.region.y + request.region.height);
    const int width = std::max(1, right - left);
    const int height = std::max(1, bottom - top);
    const QRect bounds(left, top, width, height);

    // Tight 2px padding
    const QRect patchRect =
        bounds.adjusted(-2, -2, 2, 2).intersected(QRect(0, 0, request.pageImage.width(), request.pageImage.height()));

    QImage patch(patchRect.size(), QImage::Format_RGBA8888);
    patch.fill(color);
    patch.setColorSpace(QColorSpace(QColorSpace::SRgb));

    application::ClearResponse resp;
    resp.regionId = request.regionId;
    resp.affectedBounds = domain::RectGeometry{static_cast<double>(patchRect.x()),
                                               static_cast<double>(patchRect.y()),
                                               static_cast<double>(patchRect.width()),
                                               static_cast<double>(patchRect.height())};
    resp.patch = std::move(patch);
    return resp;
}

application::ClearResponse fallbackClear(const application::ClearRequest& request) {
    return uniformClear(request, Qt::white);
}
} // namespace

QFuture<application::Result<application::ClearResponse>>
OnnxLamaPageInpainter::clear(application::ClearRequest request, application::OperationContext context) {
    if (!m_impl || !m_impl->executor) {
        return application::makeReadyFuture<application::ClearResponse>(fallbackClear(request));
    }

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

    // 1. Check if the region has a uniform background (speech bubbles, solid boxes)
    // Aligns with Koharu's fill_uniform_regions (inpainting.rs:591-620)
    if (auto uniformColor = detectUniformBackgroundColor(request.pageImage, regionRect, request.polygon)) {
        qInfo().noquote() << "[InpaintEngine] detected uniform background color=" << uniformColor->name()
                          << "for regionId=" << QString::fromStdString(request.regionId.toString())
                          << "- performing fast uniform fill";
        return application::makeReadyFuture<application::ClearResponse>(uniformClear(request, *uniformColor));
    }

    // 2. Non-uniform background (complex art, textures, screentone): Run LaMa with tight mask and 128px context
    // Expanding context around dirty bounds allows LaMa Fast Fourier Convolutions to sample
    // surrounding screentone, texture, and line art frequencies cleanly.
    // Aligns with Koharu's hd_strategy_crop_margin = 128.
    const int contextMargin = m_impl ? m_impl->manifest.tileContext : 128;
    const QRect dirtyBounds = regionRect.adjusted(-contextMargin, -contextMargin, contextMargin, contextMargin)
                                  .intersected(QRect(QPoint(0, 0), size));
    if (dirtyBounds.isEmpty()) {
        return application::makeFailedFuture<application::ClearResponse>(
            inputError(QStringLiteral("inference.invalid_input"), QStringLiteral("Region is outside image bounds")));
    }

    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);

        // Tight mask: 3px padding around text bounding box to prevent eating into surrounding artwork
        painter.drawRect(regionRect.adjusted(-3, -3, 3, 3));
    }

    QImage croppedSource = request.pageImage.copy(dirtyBounds);
    QImage croppedMask = mask.copy(dirtyBounds);
    const QRect localDirty(0, 0, dirtyBounds.width(), dirtyBounds.height());

    application::PageInpaintRequest inpaintReq;
    inpaintReq.projectId = request.projectId;
    inpaintReq.pageId = request.pageId;
    inpaintReq.sourceImage = std::move(croppedSource);
    inpaintReq.mask = croppedMask;
    inpaintReq.dirtyBounds = localDirty;

    auto promise = std::make_shared<QPromise<Result<application::ClearResponse>>>();
    promise->start();
    auto future = promise->future();

    inpaint(std::move(inpaintReq), context)
        .then([promise, request, dirtyBounds, croppedMask](Result<application::PageInpaintResponse> res) {
            if (!res) {
                if (res.error().category == application::ErrorCategory::Cancelled) {
                    promise->addResult(std::unexpected(res.error()));
                } else {
                    qWarning().noquote() << "[InpaintEngine] LaMa inpaint failed, falling back to solid fill:"
                                         << res.error().safeMessage;
                    promise->addResult(fallbackClear(request));
                }
            } else {
                const QImage& inpaintedPatch = res->image;

                // Create patch where RGB comes from the inpainted background and Alpha
                // channel strictly follows the mask (255 inside text, 0 outside).
                QImage patch(dirtyBounds.size(), QImage::Format_RGBA8888);
                patch.setColorSpace(QColorSpace(QColorSpace::SRgb));

                for (int y = 0; y < dirtyBounds.height(); ++y) {
                    const auto* srcPtr = inpaintedPatch.constScanLine(y);
                    const auto* maskPtr = croppedMask.constScanLine(y);
                    auto* dstPtr = patch.scanLine(y);
                    for (int x = 0; x < dirtyBounds.width(); ++x) {
                        dstPtr[x * 4 + 0] = srcPtr[x * 4 + 0];
                        dstPtr[x * 4 + 1] = srcPtr[x * 4 + 1];
                        dstPtr[x * 4 + 2] = srcPtr[x * 4 + 2];
                        dstPtr[x * 4 + 3] = maskPtr[x];
                    }
                }

                application::ClearResponse resp;
                resp.regionId = request.regionId;
                resp.affectedBounds = domain::RectGeometry{static_cast<double>(dirtyBounds.x()),
                                                           static_cast<double>(dirtyBounds.y()),
                                                           static_cast<double>(dirtyBounds.width()),
                                                           static_cast<double>(dirtyBounds.height())};
                resp.patch = std::move(patch);
                promise->addResult(resp);
            }
            promise->finish();
        });

    return future;
}

} // namespace manga::services
