#include "OnnxKoharuLayoutDetector.h"

#include "application/operation/TaskProgress.h"
#include "services/detection/KoharuLayoutManifest.h"
#include "services/detection/KoharuLayoutPostprocessor.h"
#include "services/detection/KoharuLayoutPreprocessor.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSemaphore>
#include <QSemaphoreReleaser>
#include <QThread>

#include <onnxruntime_cxx_api.h>
#include <vector>

namespace manga::services {

struct OnnxKoharuLayoutDetector::Impl {
    std::unique_ptr<Ort::Env> ownedEnv;
    Ort::Env* env{nullptr};
    BoundedTaskExecutor* executor{nullptr};
    QString modelDir;

    std::unique_ptr<Ort::Session> session;
    KoharuLayoutManifest manifest;

    bool loaded{false};
    int activeInferences{0};
    QMutex loadMutex;
    QSemaphore concurrencyGate{1};
};

OnnxKoharuLayoutDetector::OnnxKoharuLayoutDetector(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

OnnxKoharuLayoutDetector::OnnxKoharuLayoutDetector(std::unique_ptr<Ort::Env> env,
                                                   BoundedTaskExecutor* executor,
                                                   QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->ownedEnv = std::move(env);
    m_impl->env = m_impl->ownedEnv.get();
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

OnnxKoharuLayoutDetector::~OnnxKoharuLayoutDetector() = default;

void OnnxKoharuLayoutDetector::release() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->activeInferences == 0 && m_impl->loaded) {
        m_impl->session.reset();
        m_impl->loaded = false;
        qInfo().noquote() << "[DetectEngine] ONNX layout detection session released from memory";
    }
}

application::Result<void> OnnxKoharuLayoutDetector::ensureLoaded() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->loaded) {
        return {};
    }

    auto manifestResult = loadAndValidateDetectionManifest(m_impl->modelDir);
    if (!manifestResult.has_value()) {
        return std::unexpected(manifestResult.error());
    }
    m_impl->manifest = std::move(manifestResult.value());

    if (m_impl->env == nullptr) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Internal,
                                              QStringLiteral("detection.null_env"),
                                              QStringLiteral("ONNX Runtime Environment pointer is null")));
    }

    const QString onnxPath = QDir(m_impl->modelDir).filePath(m_impl->manifest.modelFile);
    qInfo().noquote() << "[DetectEngine] loading Koharu layout detection ONNX model from" << onnxPath;

    try {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(2);
        sessionOptions.SetInterOpNumThreads(1);
        sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, onnxPath.toStdWString().c_str(), sessionOptions);
#else
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, onnxPath.toStdString().c_str(), sessionOptions);
#endif
    } catch (const Ort::Exception& ex) {
        qWarning().noquote() << "[DetectEngine] failed to create ONNX session:" << ex.what();
        auto error =
            application::makeApplicationError(application::ErrorCategory::Model,
                                              QStringLiteral("model.corrupt"),
                                              QStringLiteral("Failed to load Koharu layout detection ONNX session"),
                                              application::RecoveryAction::LocateModel);
        error.diagnostic = QString::fromUtf8(ex.what());
        return std::unexpected(error);
    }

    m_impl->loaded = true;
    qInfo().noquote() << "[DetectEngine] model loaded successfully (" << m_impl->manifest.inputWidth << "x"
                      << m_impl->manifest.inputHeight << ")";
    return {};
}

QFuture<application::Result<application::TextDetectionResponse>>
OnnxKoharuLayoutDetector::detect(application::TextDetectionRequest request, application::OperationContext context) {
    if (m_impl->executor == nullptr) {
        QPromise<application::Result<application::TextDetectionResponse>> promise;
        promise.start();
        promise.addResult(
            std::unexpected(application::makeApplicationError(application::ErrorCategory::Internal,
                                                              QStringLiteral("detection.null_executor"),
                                                              QStringLiteral("BoundedTaskExecutor is null"))));
        promise.finish();
        return promise.future();
    }

    auto submitRes = m_impl->executor->submit<application::TextDetectionResponse>(
        application::OperationStage::Detect,
        TaskQueue::Interactive,
        context,
        [this, req = std::move(request)](const application::OperationContext& ctx,
                                         application::TaskProgress& progress) {
            return runInference(req, ctx, progress);
        });

    if (!submitRes.has_value()) {
        QPromise<application::Result<application::TextDetectionResponse>> promise;
        promise.start();
        promise.addResult(std::unexpected(submitRes.error()));
        promise.finish();
        return promise.future();
    }

    return submitRes.value();
}

application::Result<application::TextDetectionResponse>
OnnxKoharuLayoutDetector::runInference(const application::TextDetectionRequest& request,
                                       const application::OperationContext& context,
                                       application::TaskProgress& progress) {
    if (context.stopToken.stop_requested()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.user_requested"),
                                                                 QStringLiteral("Detection cancelled by user")));
    }

    progress.setRange(0, 100);
    progress.setValue(10);
    progress.setText(QStringLiteral("Loading detection model..."));
    auto loadResult = ensureLoaded();
    if (!loadResult.has_value()) {
        return std::unexpected(loadResult.error());
    }

    {
        QMutexLocker locker(&m_impl->loadMutex);
        ++m_impl->activeInferences;
    }
    struct InferenceGuard {
        OnnxKoharuLayoutDetector::Impl* impl;
        ~InferenceGuard() {
            QMutexLocker locker(&impl->loadMutex);
            --impl->activeInferences;
        }
    } guard{m_impl.get()};

    if (context.stopToken.stop_requested()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.user_requested"),
                                                                 QStringLiteral("Detection cancelled by user")));
    }

    QElapsedTimer timer;
    timer.start();

    qInfo().noquote() << "[DetectEngine] start inference page=" << QString::fromStdString(request.pageId.toString())
                      << "imgSize=" << request.image.size() << "minConf=" << request.minConfidence << "readingDir="
                      << (request.readingDirection == domain::ReadingDirection::RightToLeft ? "RTL" : "LTR");

    progress.setValue(30);
    progress.setText(QStringLiteral("Preprocessing page image..."));
    const auto preprocessed =
        KoharuLayoutPreprocessor::process(request.image, m_impl->manifest.inputWidth, m_impl->manifest.inputHeight);

    if (context.stopToken.stop_requested()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.user_requested"),
                                                                 QStringLiteral("Detection cancelled by user")));
    }

    m_impl->concurrencyGate.acquire(1);
    QSemaphoreReleaser releaser(&m_impl->concurrencyGate, 1);

    progress.setValue(50);
    progress.setText(QStringLiteral("Running layout inference..."));

    try {
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        const int64_t inputDims[] = {1, 3, m_impl->manifest.inputHeight, m_impl->manifest.inputWidth};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float*>(preprocessed.tensorData.data()), preprocessed.tensorData.size(), inputDims, 4);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t inputCount = m_impl->session->GetInputCount();
        const size_t outputCount = m_impl->session->GetOutputCount();

        std::vector<Ort::AllocatedStringPtr> inputNamePtrs;
        std::vector<const char*> inputNames;
        for (size_t i = 0; i < inputCount; ++i) {
            inputNamePtrs.push_back(m_impl->session->GetInputNameAllocated(i, allocator));
            inputNames.push_back(inputNamePtrs.back().get());
        }

        std::vector<Ort::AllocatedStringPtr> outputNamePtrs;
        std::vector<const char*> outputNames;
        for (size_t i = 0; i < outputCount; ++i) {
            outputNamePtrs.push_back(m_impl->session->GetOutputNameAllocated(i, allocator));
            outputNames.push_back(outputNamePtrs.back().get());
        }

        std::vector<Ort::Value> inputTensors;
        inputTensors.push_back(std::move(inputTensor));

        // If second input exists (e.g. orig_target_sizes), populate it
        int64_t targetSizesData[2] = {request.image.height(), request.image.width()};
        if (inputCount >= 2) {
            const int64_t targetSizesDims[] = {1, 2};
            Ort::Value targetSizesTensor =
                Ort::Value::CreateTensor<int64_t>(memInfo, targetSizesData, 2, targetSizesDims, 2);
            inputTensors.push_back(std::move(targetSizesTensor));
        }

        auto outputTensors = m_impl->session->Run(Ort::RunOptions{nullptr},
                                                  inputNames.data(),
                                                  inputTensors.data(),
                                                  inputTensors.size(),
                                                  outputNames.data(),
                                                  outputNames.size());

        qInfo().noquote() << "[DetectEngine] ONNX model run completed in" << timer.elapsed() << "ms";

        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.user_requested"),
                                                                     QStringLiteral("Detection cancelled by user")));
        }

        progress.setValue(80);
        progress.setText(QStringLiteral("Postprocessing layout regions..."));

        KoharuLayoutPostprocessor::RawOutputs raw;
        for (size_t i = 0; i < outputCount; ++i) {
            const QString name = QString::fromUtf8(outputNames[i]);
            auto typeInfo = outputTensors[i].GetTensorTypeAndShapeInfo();
            auto shape = typeInfo.GetShape();

            if (name == m_impl->manifest.detsOutputName || name.contains(QStringLiteral("box")) ||
                name.contains(QStringLiteral("det"))) {
                raw.dets = outputTensors[i].GetTensorData<float>();
                raw.detsShape = std::move(shape);
            } else if (name == m_impl->manifest.labelsOutputName || name.contains(QStringLiteral("label")) ||
                       name.contains(QStringLiteral("class"))) {
                raw.labels = outputTensors[i].GetTensorData<int64_t>();
                raw.labelsShape = std::move(shape);
            } else if (name == m_impl->manifest.masksOutputName || name.contains(QStringLiteral("mask"))) {
                raw.masks = outputTensors[i].GetTensorData<float>();
                raw.masksShape = std::move(shape);
            }
        }

        auto response = KoharuLayoutPostprocessor::process(
            request.pageId, raw, preprocessed, request.minConfidence, request.readingDirection);

        qInfo().noquote() << "[DetectEngine] result page=" << QString::fromStdString(request.pageId.toString())
                          << "regions=" << response.detectedRegions.size() << "panels=" << response.panels.size()
                          << "totalElapsed=" << timer.elapsed() << "ms";

        progress.setValue(100);
        progress.setText(QStringLiteral("Detection completed"));
        return response;

    } catch (const Ort::Exception& ex) {
        qWarning().noquote() << "[DetectEngine] inference failed:" << ex.what();
        auto error =
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.failed"),
                                              QStringLiteral("ONNX Runtime layout detection inference failed"));
        error.diagnostic = QString::fromUtf8(ex.what());
        return std::unexpected(error);
    }
}

} // namespace manga::services
