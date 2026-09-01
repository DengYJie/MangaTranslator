#include "OnnxLamaPageInpainter.h"

#include "application/operation/AsyncResult.h"
#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QColorSpace>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>

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
    QMutex runMutex;
};

OnnxLamaPageInpainter::OnnxLamaPageInpainter(Ort::Env* env, BoundedTaskExecutor* executor, InpaintManifest manifest)
    : m_impl(std::make_shared<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->manifest = std::move(manifest);
    if (env == nullptr)
        return;
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    try {
#ifdef _WIN32
        const auto path = QDir(m_impl->manifest.modelDirectory).filePath(m_impl->manifest.modelFile).toStdWString();
        m_impl->session = std::make_unique<Ort::Session>(*env, path.c_str(), options);
#else
        const auto path = QDir(m_impl->manifest.modelDirectory).filePath(m_impl->manifest.modelFile).toStdString();
        m_impl->session = std::make_unique<Ort::Session>(*env, path.c_str(), options);
#endif
    } catch (...) {
        // Loading is reported by inpaint rather than from a GUI-thread constructor.
        m_impl->session.reset();
    }
}

OnnxLamaPageInpainter::~OnnxLamaPageInpainter() = default;

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
        [impl, request = std::move(request)](
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
            if (!impl->session)
                return std::unexpected(modelError(QStringLiteral("model.load_failed"),
                                                  QStringLiteral("The inpainting model could not be loaded")));

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
            application::PageInpaintResponse response{
                request.projectId, request.pageId, std::move(result), request.dirtyBounds};
            return response;
        });
    if (!submitted)
        return application::makeFailedFuture<application::PageInpaintResponse>(submitted.error());
    return *submitted;
}

} // namespace manga::services
