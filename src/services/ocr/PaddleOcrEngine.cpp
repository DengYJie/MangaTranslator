#include "PaddleOcrEngine.h"

#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSemaphore>
#include <QSemaphoreReleaser>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <vector>

namespace manga::services {

struct PaddleOcrEngine::Impl {
    std::unique_ptr<Ort::Env> ownedEnv;
    Ort::Env* env{nullptr};
    BoundedTaskExecutor* executor{nullptr};
    QString modelDir;

    std::unique_ptr<Ort::Session> session;
    QStringList keys;

    bool loaded{false};
    bool available{false};
    int activeInferences{0};
    QMutex loadMutex;
    QSemaphore concurrencyGate{1};
};

PaddleOcrEngine::PaddleOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

PaddleOcrEngine::PaddleOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->ownedEnv = std::move(env);
    m_impl->env = m_impl->ownedEnv.get();
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

PaddleOcrEngine::~PaddleOcrEngine() = default;

void PaddleOcrEngine::release() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->activeInferences == 0 && m_impl->loaded) {
        m_impl->session.reset();
        m_impl->loaded = false;
        qInfo().noquote() << "[PaddleOcrEngine] session released from memory";
    }
}

bool PaddleOcrEngine::isAvailable() const {
    if (m_impl->modelDir.trimmed().isEmpty()) {
        return false;
    }
    const QDir dir(m_impl->modelDir);
    const QStringList candidateModels = {QStringLiteral("ppocr_rec.onnx"),
                                         QStringLiteral("rec.onnx"),
                                         QStringLiteral("paddle_rec.onnx"),
                                         QStringLiteral("ch_PP-OCRv4_rec_infer.onnx"),
                                         QStringLiteral("model.onnx")};
    for (const auto& name : candidateModels) {
        if (dir.exists(name)) {
            return true;
        }
    }
    return false;
}

application::Result<void> PaddleOcrEngine::ensureLoaded() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->loaded) {
        return {};
    }

    if (!m_impl->env) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.env_missing"),
                                                                 QStringLiteral("ONNX environment is null")));
    }

    const QDir dir(m_impl->modelDir);
    QString modelPath;
    const QStringList candidateModels = {QStringLiteral("ppocr_rec.onnx"),
                                         QStringLiteral("rec.onnx"),
                                         QStringLiteral("paddle_rec.onnx"),
                                         QStringLiteral("ch_PP-OCRv4_rec_infer.onnx"),
                                         QStringLiteral("model.onnx")};
    for (const auto& name : candidateModels) {
        if (dir.exists(name)) {
            modelPath = dir.filePath(name);
            break;
        }
    }

    if (modelPath.isEmpty()) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Model,
                                              QStringLiteral("model.missing"),
                                              QStringLiteral("PaddleOCR model file not found in directory"),
                                              application::RecoveryAction::LocateModel));
    }

    QString dictPath;
    const QStringList candidateDicts = {QStringLiteral("ppocr_keys_v1.txt"),
                                        QStringLiteral("keys.txt"),
                                        QStringLiteral("dict.txt"),
                                        QStringLiteral("ppocr_keys.txt")};
    for (const auto& name : candidateDicts) {
        if (dir.exists(name)) {
            dictPath = dir.filePath(name);
            break;
        }
    }

    m_impl->keys.clear();
    if (!dictPath.isEmpty()) {
        QFile dictFile(dictPath);
        if (dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&dictFile);
            while (!stream.atEnd()) {
                QString line = stream.readLine();
                m_impl->keys.append(line);
            }
        }
    }

    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(2);
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    try {
#ifdef _WIN32
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, modelPath.toStdWString().c_str(), options);
#else
        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, modelPath.toStdString().c_str(), options);
#endif
        m_impl->loaded = true;
        m_impl->available = true;
        qInfo().noquote() << "[PaddleOcrEngine] loaded successfully model=" << modelPath
                          << "dictSize=" << m_impl->keys.size();
    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleOcrEngine] failed to create session:" << e.what();
        return std::unexpected(application::makeApplicationError(
            application::ErrorCategory::Model, QStringLiteral("model.load_failed"), QString::fromStdString(e.what())));
    }

    return {};
}

QFuture<application::Result<application::OcrResponse>>
PaddleOcrEngine::recognize(application::OcrRequest request, application::OperationContext context) {
    if (m_impl->executor == nullptr) {
        QPromise<application::Result<application::OcrResponse>> promise;
        promise.start();
        promise.addResult(std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                            QStringLiteral("executor.null"),
                                                                            QStringLiteral("Task executor is null"))));
        promise.finish();
        return promise.future();
    }

    auto submitRes = m_impl->executor->submit<application::OcrResponse>(
        application::OperationStage::Ocr,
        TaskQueue::Interactive,
        context,
        [this, req = std::move(request)](const application::OperationContext& ctx,
                                         application::TaskProgress& progress) {
            return runInference(req, ctx, progress);
        });

    if (!submitRes.has_value()) {
        QPromise<application::Result<application::OcrResponse>> promise;
        promise.start();
        promise.addResult(std::unexpected(submitRes.error()));
        promise.finish();
        return promise.future();
    }

    return submitRes.value();
}

application::Result<application::OcrResponse>
PaddleOcrEngine::runInference(const application::OcrRequest& request,
                              const application::OperationContext& context,
                              application::TaskProgress& progress) {
    progress.setRange(0, 1);
    progress.setValue(0);
    progress.setText(QStringLiteral("Loading PaddleOCR model..."));

    auto loadRes = ensureLoaded();
    if (!loadRes.has_value()) {
        return std::unexpected(loadRes.error());
    }

    {
        QMutexLocker locker(&m_impl->loadMutex);
        ++m_impl->activeInferences;
    }
    struct InferenceGuard {
        PaddleOcrEngine::Impl* impl;
        ~InferenceGuard() {
            QMutexLocker locker(&impl->loadMutex);
            --impl->activeInferences;
        }
    } guard{m_impl.get()};

    m_impl->concurrencyGate.acquire(1);
    QSemaphoreReleaser releaser(&m_impl->concurrencyGate, 1);

    if (context.stopToken.stop_requested()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.user_requested"),
                                                                 QStringLiteral("OCR cancelled")));
    }

    if (request.image.isNull() || request.rect.width <= 0 || request.rect.height <= 0) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.invalid_image"),
                                                                 QStringLiteral("Invalid crop region")));
    }

    const QRect cropRect(std::clamp(qRound(request.rect.x), 0, request.image.width() - 1),
                         std::clamp(qRound(request.rect.y), 0, request.image.height() - 1),
                         std::clamp(qRound(request.rect.width), 1, request.image.width()),
                         std::clamp(qRound(request.rect.height), 1, request.image.height()));

    QImage cropped = request.image.copy(cropRect).convertToFormat(QImage::Format_RGB888);
    if (cropped.isNull() || cropped.width() == 0 || cropped.height() == 0) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.crop_empty"),
                                                                 QStringLiteral("Cropped image is empty")));
    }

    // A fixed row height with a width derived from the crop's own aspect ratio, clamped to the accepted
    // range and rounded up to a multiple of 4. The scaling below may ignore the aspect ratio because this
    // width already carries it.
    constexpr int targetH = 48;
    int targetW = std::clamp(qRound(cropped.width() * (static_cast<double>(targetH) / cropped.height())), 16, 960);
    targetW = ((targetW + 3) / 4) * 4;

    QImage scaled = cropped.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Channels-first float buffer with each pixel normalized as (value / 255.0 - 0.5) / 0.5.
    std::vector<float> inputData(1 * 3 * targetH * targetW);
    for (int y = 0; y < targetH; ++y) {
        const uchar* line = scaled.scanLine(y);
        for (int x = 0; x < targetW; ++x) {
            const float r = (line[x * 3 + 0] / 255.0f - 0.5f) / 0.5f;
            const float g = (line[x * 3 + 1] / 255.0f - 0.5f) / 0.5f;
            const float b = (line[x * 3 + 2] / 255.0f - 0.5f) / 0.5f;

            inputData[0 * (targetH * targetW) + y * targetW + x] = r;
            inputData[1 * (targetH * targetW) + y * targetW + x] = g;
            inputData[2 * (targetH * targetW) + y * targetW + x] = b;
        }
    }

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::vector<int64_t> inputShape = {1, 3, targetH, targetW};
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memInfo, inputData.data(), inputData.size(), inputShape.data(), inputShape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = m_impl->session->GetInputNameAllocated(0, allocator);
    auto outputName = m_impl->session->GetOutputNameAllocated(0, allocator);

    const char* inputNames[] = {inputName.get()};
    const char* outputNames[] = {outputName.get()};

    QElapsedTimer timer;
    timer.start();

    try {
        auto outputs = m_impl->session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        if (outputs.empty()) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Inference,
                                                  QStringLiteral("inference.empty_output"),
                                                  QStringLiteral("No outputs returned from PaddleOCR")));
        }

        auto& logitsTensor = outputs[0];
        auto typeInfo = logitsTensor.GetTensorTypeAndShapeInfo();
        auto shape = typeInfo.GetShape();

        if (shape.size() < 3 || shape[0] < 1 || shape[1] < 1 || shape[2] < 1) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                     QStringLiteral("inference.bad_shape"),
                                                                     QStringLiteral("Invalid logits output shape")));
        }

        const int64_t seqLen = shape[1];
        const int64_t numClasses = shape[2];
        const float* logits = logitsTensor.GetTensorData<float>();

        // CTC Greedy Decoding
        QString decodedText;
        int lastIndex = -1;
        double totalConfidence = 0.0;
        int count = 0;

        for (int64_t t = 0; t < seqLen; ++t) {
            const float* timestepLogits = logits + t * numClasses;
            int argmax = 0;
            float maxVal = timestepLogits[0];
            for (int64_t c = 1; c < numClasses; ++c) {
                if (timestepLogits[c] > maxVal) {
                    maxVal = timestepLogits[c];
                    argmax = static_cast<int>(c);
                }
            }

            // Index 0 is CTC Blank
            if (argmax > 0 && argmax != lastIndex) {
                const int keyIdx = argmax - 1;
                if (keyIdx < m_impl->keys.size()) {
                    decodedText.append(m_impl->keys.at(keyIdx));
                    totalConfidence += std::clamp(static_cast<double>(maxVal), 0.0, 1.0);
                    count++;
                }
            }
            lastIndex = argmax;
        }

        application::OcrResponse response;
        response.regionId = request.regionId;
        response.text = decodedText;
        response.confidence = count > 0 ? (totalConfidence / count) : 0.0;

        qInfo().noquote() << "[PaddleOcrEngine] recognized region="
                          << QString::fromStdString(request.regionId.toString()) << "text=\"" << decodedText
                          << "\" elapsed=" << timer.elapsed() << "ms";
        progress.setValue(1);
        progress.setText(QStringLiteral("PaddleOCR complete"));
        return response;

    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleOcrEngine] inference exception:" << e.what();
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.failed"),
                                                                 QString::fromStdString(e.what())));
    }
}

} // namespace manga::services
