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
#include <atomic>
#include <cmath>
#include <cstdint>
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

/// Diagnostics for the residual-text investigation. File dumps stay behind an environment
/// variable so a normal run only pays for the one-line summaries.
struct MaskStats {
    int total{0};
    int set{0};
    int minAlpha{0};
    int maxAlpha{0};
    double meanAlpha{0.0};
};

MaskStats measureMask(const QImage& mask) {
    MaskStats stats;
    if (mask.isNull()) {
        return stats;
    }
    const QImage gray =
        mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    stats.total = gray.width() * gray.height();
    stats.minAlpha = 255;
    std::uint64_t sum = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) {
            const int value = line[x];
            if (value == 0) {
                continue;
            }
            ++stats.set;
            sum += std::uint64_t(value);
            stats.minAlpha = std::min(stats.minAlpha, value);
            stats.maxAlpha = std::max(stats.maxAlpha, value);
        }
    }
    if (stats.set == 0) {
        stats.minAlpha = 0;
    } else {
        stats.meanAlpha = double(sum) / double(stats.set);
    }
    return stats;
}

const QString& debugDumpDirectory() {
    static const QString path = []() {
        const QByteArray raw = qgetenv("MANGA_INPAINT_DEBUG_DIR");
        if (raw.isEmpty()) {
            return QString();
        }
        QDir dir{QString::fromLocal8Bit(raw)};
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            return QString();
        }
        return dir.absolutePath();
    }();
    return path;
}

std::atomic<std::uint64_t> g_debugSequence{0};

void dumpDebugImage(const QString& tag, const QImage& image) {
    const QString& dir = debugDumpDirectory();
    if (dir.isEmpty() || image.isNull()) {
        return;
    }
    const std::uint64_t seq = g_debugSequence.fetch_add(1, std::memory_order_relaxed);
    const QString fileName = QStringLiteral("%1_%2_%3x%4.png").arg(seq).arg(tag).arg(image.width()).arg(image.height());
    const QString filePath = QDir(dir).filePath(fileName);
    if (image.save(filePath)) {
        qInfo().noquote() << "[InpaintDebug] wrote" << filePath;
    } else {
        qWarning().noquote() << "[InpaintDebug] failed to write" << filePath;
    }
}

void dumpDebugMaskOverlay(const QString& tag, const QImage& base, const QImage& mask) {
    const QString& dir = debugDumpDirectory();
    if (dir.isEmpty() || base.isNull() || mask.isNull()) {
        return;
    }
    QImage overlay = base.convertToFormat(QImage::Format_RGBA8888);
    const QImage gray =
        mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    if (overlay.size() != gray.size()) {
        return;
    }
    for (int y = 0; y < overlay.height(); ++y) {
        auto* line = reinterpret_cast<uchar*>(overlay.scanLine(y));
        const uchar* maskLine = gray.constScanLine(y);
        for (int x = 0; x < overlay.width(); ++x) {
            const int alpha = maskLine[x];
            if (alpha == 0) {
                continue;
            }
            const float weight = float(alpha) * kInv255;
            line[x * 4] = uchar(line[x * 4] * (1.0f - weight) + 255.0f * weight);
            line[x * 4 + 1] = uchar(line[x * 4 + 1] * (1.0f - weight) + 32.0f * weight);
            line[x * 4 + 2] = uchar(line[x * 4 + 2] * (1.0f - weight) + 32.0f * weight);
        }
    }
    dumpDebugImage(tag, overlay);
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
            const LamaTilePlan plan = planLamaTiles(planningMask,
                                                    QRect(QPoint(0, 0), planningMask.size()),
                                                    impl->manifest.tileCore,
                                                    impl->manifest.tileContext);
            const std::vector<LamaTile>& tiles = plan.tiles;
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
            std::int64_t writtenPixels = 0;
            for (size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
                if (auto r = checkpoint(ctx); !r)
                    return std::unexpected(r.error());
                const LamaTile& tile = tiles[tileIndex];
                const QRect corePage = tile.core.translated(offset);
                const QRect cropPage = tile.crop.translated(offset);
                const MaskStats tileMask = measureMask(mask.copy(cropPage));
                qDebug().noquote() << "[InpaintEngine] tile" << int(tileIndex) << "labels=" << tile.labels.size()
                                   << "core=" << corePage << "crop=" << cropPage << "cropMask=" << tileMask.set << '/'
                                   << tileMask.total << "alpha[min/mean/max]=" << tileMask.minAlpha
                                   << tileMask.meanAlpha << tileMask.maxAlpha;
                if (tileIndex == 0) {
                    dumpDebugImage(QStringLiteral("tile0_source"), source.copy(cropPage));
                    dumpDebugImage(QStringLiteral("tile0_mask"), mask.copy(cropPage));
                }
                std::vector<int> xIndices, yIndices;
                buildSymmetricPadIndices(cropPage.width(), padMultiple, xIndices);
                buildSymmetricPadIndices(cropPage.height(), padMultiple, yIndices);
                const int width = int(xIndices.size());
                const int height = int(yIndices.size());
                const size_t plane = size_t(width) * height;
                std::vector<float> rgb(size_t(3) * plane, 0.0f), binary(plane, 0.0f);
                for (int py = 0; py < height; ++py) {
                    const int sy = cropPage.top() + yIndices[py];
                    const auto* sp = result.constScanLine(sy);
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
                    QElapsedTimer onnxTimer;
                    onnxTimer.start();
                    outputs = impl->session->Run(Ort::RunOptions{nullptr}, inputNames, tensors, 2, outputNames, 1);
                    qDebug().noquote() << "[InpaintEngine] ONNX Run took" << onnxTimer.elapsed() << "ms for tile"
                                       << tileIndex;
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
                if (outShape.size() != 4 || outShape[0] != 1 || outShape[1] != 3)
                    return std::unexpected(modelError(QStringLiteral("model.incompatible"),
                                                      QStringLiteral("Inpainting output is not an NCHW RGB tensor")));
                const int outHeight = int(outShape[2]);
                const int outWidth = int(outShape[3]);
                if (outHeight <= 0 || outWidth <= 0)
                    return std::unexpected(
                        modelError(QStringLiteral("model.incompatible"),
                                   QStringLiteral("Inpainting output has a non-positive spatial size")));
                const float* values = outputs[0].GetTensorData<float>();
                const size_t outPlane = size_t(outWidth) * outHeight;
                QImage generated(outWidth, outHeight, QImage::Format_RGB888);
                if (generated.isNull())
                    return std::unexpected(modelError(QStringLiteral("model.incompatible"),
                                                      QStringLiteral("Could not allocate generated tile image")));
                for (int gy = 0; gy < outHeight; ++gy) {
                    uchar* line = generated.scanLine(gy);
                    for (int gx = 0; gx < outWidth; ++gx) {
                        const size_t p = size_t(gy) * outWidth + gx;
                        for (int c = 0; c < 3; ++c) {
                            line[gx * 3 + c] =
                                static_cast<uchar>(std::clamp(values[c * outPlane + p], 0.0f, 1.0f) * 255.0f + 0.5f);
                        }
                    }
                }
                if (generated.width() != width || generated.height() != height) {
                    generated = generated.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }

                // Hard writeback gated by connected-component label. Each tile owns a set of
                // component labels, so overlapping crops cannot clobber another tile's pixels.
                for (int y = tile.core.top(); y <= tile.core.bottom(); ++y) {
                    for (int x = tile.core.left(); x <= tile.core.right(); ++x) {
                        const quint32 label = plan.labels[size_t(y) * plan.width + x];
                        if (!std::binary_search(tile.labels.constBegin(), tile.labels.constEnd(), label))
                            continue;
                        const int sx = offset.x() + x;
                        const int sy = offset.y() + y;
                        if (sx < 0 || sx >= result.width() || sy < 0 || sy >= result.height())
                            continue;
                        const int px = sx - cropPage.left();
                        const int py = sy - cropPage.top();
                        if (px < 0 || px >= width || py < 0 || py >= height)
                            continue;
                        const uchar* g = generated.constScanLine(py) + px * 3;
                        uchar* d = result.scanLine(sy) + sx * 4;
                        d[0] = g[0];
                        d[1] = g[1];
                        d[2] = g[2];
                        ++writtenPixels;
                    }
                }
                progress.setValue(int(tileIndex) + 1);
            }
            qInfo().noquote() << "[InpaintEngine] inpainting completed in" << timer.elapsed() << "ms"
                              << "written=" << writtenPixels;
            dumpDebugImage(QStringLiteral("inpaint_result"), result.copy(dirty));
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

    const MaskStats clearStats = measureMask(clearMask.mask);
    qInfo().noquote() << "[InpaintEngine] clear region=" << regionRect << "bounds=" << clearMask.dirtyBounds
                      << "page=" << size << "margin=" << contextMargin << "polygon="
                      << (request.polygon && request.polygon->isValid() ? int(request.polygon->points.size()) : 0)
                      << "mask=" << clearStats.set << '/' << clearStats.total
                      << "alpha[min/mean/max]=" << clearStats.minAlpha << clearStats.meanAlpha << clearStats.maxAlpha;
    dumpDebugImage(QStringLiteral("clear_mask"), clearMask.mask);
    dumpDebugMaskOverlay(QStringLiteral("clear_overlay"), page.copy(clearMask.dirtyBounds), clearMask.mask);

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
                qInfo().noquote() << "[InpaintEngine] clear lama path regionId="
                                  << QString::fromStdString(request.regionId.toString())
                                  << "inpainted=" << res->image.size() << "patch=" << response.patch.size();
                dumpDebugImage(QStringLiteral("clear_inpainted"), res->image);
                dumpDebugImage(QStringLiteral("clear_patch"), response.patch);
                promise->addResult(response);
            }
            promise->finish();
        });

    return future;
}

} // namespace manga::services
