#include "OnnxMangaOcrEngine.h"

#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/ocr/BaberuManifest.h"
#include "services/ocr/BaberuOcrPreprocessor.h"
#include "services/ocr/BaberuVocab.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSemaphore>
#include <QSemaphoreReleaser>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <new>
#include <onnxruntime_cxx_api.h>
#include <optional>
#include <set>
#include <vector>

namespace manga::services {

struct OnnxMangaOcrEngine::Impl {
    Ort::Env* env{nullptr};
    BoundedTaskExecutor* executor{nullptr};
    QString modelDir;

    std::unique_ptr<Ort::Session> visSession;
    std::unique_ptr<Ort::Session> preSession;
    std::unique_ptr<Ort::Session> stepSession;

    OcrModelManifest manifest;
    std::optional<BaberuVocab> vocab;

    bool loaded{false};
    QMutex loadMutex;
    QSemaphore concurrencyGate{1}; // concurrency gate = 1
};

OnnxMangaOcrEngine::OnnxMangaOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

OnnxMangaOcrEngine::~OnnxMangaOcrEngine() = default;

application::Result<void> OnnxMangaOcrEngine::ensureLoaded() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->loaded) {
        return {};
    }

    auto manifestResult = loadAndValidateManifest(m_impl->modelDir);
    if (!manifestResult.has_value()) {
        return std::unexpected<application::ApplicationError>(manifestResult.error());
    }
    m_impl->manifest = std::move(manifestResult.value());

    QString vocabPath = QDir(m_impl->modelDir).filePath(m_impl->manifest.tokenizer.vocabFile);
    auto vocabResult = BaberuVocab::load(vocabPath,
                                         m_impl->manifest.tokenizer.bosTokenId,
                                         m_impl->manifest.tokenizer.eosTokenId,
                                         m_impl->manifest.tokenizer.padTokenId,
                                         m_impl->manifest.tokenizer.unkTokenId);
    if (!vocabResult.has_value()) {
        return std::unexpected<application::ApplicationError>(vocabResult.error());
    }
    m_impl->vocab = std::move(vocabResult.value());

    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Autoregressive decoding amplifies small kernel differences, so deterministic
    // implementations are required whenever ONNX Runtime provides them.
    options.SetDeterministicCompute(true);
    options.SetIntraOpNumThreads(std::clamp(QThread::idealThreadCount() - 1, 1, 4));
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    try {
        QString visPath, prePath, stepPath;
        for (const auto& g : m_impl->manifest.graphs) {
            if (g.role == QStringLiteral("vision"))
                visPath = g.file;
            else if (g.role == QStringLiteral("decoder-prefill") || g.role == QStringLiteral("prefill"))
                prePath = g.file;
            else if (g.role == QStringLiteral("decoder-step") || g.role == QStringLiteral("step"))
                stepPath = g.file;
        }

#ifdef _WIN32
        m_impl->visSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(visPath).toStdWString().c_str(), options);
        m_impl->preSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(prePath).toStdWString().c_str(), options);
        m_impl->stepSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(stepPath).toStdWString().c_str(), options);
#else
        m_impl->visSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(visPath).toStdString().c_str(), options);
        m_impl->preSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(prePath).toStdString().c_str(), options);
        m_impl->stepSession = std::make_unique<Ort::Session>(
            *m_impl->env, QDir(m_impl->modelDir).filePath(stepPath).toStdString().c_str(), options);
#endif
    } catch (const Ort::Exception& e) {
        return std::unexpected<application::ApplicationError>(application::makeApplicationError(
            application::ErrorCategory::Model, QStringLiteral("model.load_failed"), QString::fromStdString(e.what())));
    }

    // Verify names
    auto checkSession = [](Ort::Session& session, const char* name) -> application::Result<void> {
        size_t inCount = session.GetInputCount();
        size_t outCount = session.GetOutputCount();
        if (inCount == 0 || outCount == 0) {
            return std::unexpected<application::ApplicationError>(application::makeApplicationError(
                application::ErrorCategory::Model,
                QStringLiteral("model.incompatible"),
                QStringLiteral("Session has no inputs or outputs: ") + QString::fromLatin1(name)));
        }
        return {};
    };
    if (auto r = checkSession(*m_impl->visSession, "vision"); !r.has_value())
        return r;
    if (auto r = checkSession(*m_impl->preSession, "prefill"); !r.has_value())
        return r;
    if (auto r = checkSession(*m_impl->stepSession, "step"); !r.has_value())
        return r;

    m_impl->loaded = true;
    return {};
}

QFuture<application::Result<application::OcrResponse>>
OnnxMangaOcrEngine::recognize(application::OcrRequest request, application::OperationContext context) {
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
OnnxMangaOcrEngine::runInference(const application::OcrRequest& request,
                                 const application::OperationContext& context,
                                 application::TaskProgress& progress) {
    progress.setRange(0, 1);
    progress.setValue(0);
    progress.setText(QStringLiteral("Loading model..."));
    auto loadRes = ensureLoaded();
    if (!loadRes.has_value()) {
        return std::unexpected<application::ApplicationError>(loadRes.error());
    }

    m_impl->concurrencyGate.acquire(1);
    QSemaphoreReleaser releaser(&m_impl->concurrencyGate, 1);

    if (context.stopToken.stop_requested()) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Cancelled,
                                              QStringLiteral("cancelled.user_requested"),
                                              QStringLiteral("OCR cancelled before preprocess")));
    }

    auto preResult = BaberuOcrPreprocessor::preprocess(request, m_impl->manifest);
    if (!preResult.has_value()) {
        return std::unexpected<application::ApplicationError>(preResult.error());
    }
    std::vector<float>& inputTensorValues = preResult.value();

    if (context.stopToken.stop_requested()) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Cancelled,
                                              QStringLiteral("cancelled.user_requested"),
                                              QStringLiteral("OCR cancelled after preprocess")));
    }

    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<int64_t> inputShape = {1, 3, m_impl->manifest.input.height, m_impl->manifest.input.width};

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputTensorValues.data(), inputTensorValues.size(), inputShape.data(), inputShape.size());

    const char* visInputNames[] = {"pixel_values"};
    const char* visOutputNames[] = {"vision_embeds"};

    std::vector<Ort::Value> visOutputs;
    try {
        visOutputs =
            m_impl->visSession->Run(Ort::RunOptions{nullptr}, visInputNames, &inputTensor, 1, visOutputNames, 1);
    } catch (const Ort::Exception& e) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.failed"),
                                              QString::fromStdString(e.what())));
    } catch (const std::bad_alloc&) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.oom"),
                                              QStringLiteral("OOM during vision inference"),
                                              application::RecoveryAction::Retry,
                                              true));
    }

    if (context.stopToken.stop_requested()) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Cancelled,
                                              QStringLiteral("cancelled.user_requested"),
                                              QStringLiteral("OCR cancelled after vision inference")));
    }

    Ort::Value& visionEmbeds = visOutputs.front();
    const auto visionShape = visionEmbeds.GetTensorTypeAndShapeInfo().GetShape();
    if (visionShape.size() < 2 || visionShape[1] <= 0) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Model,
                                              QStringLiteral("model.incompatible"),
                                              QStringLiteral("Vision output has an invalid sequence dimension")));
    }
    int64_t decoderPosition = visionShape[1] + 1;
    int64_t bosId = m_impl->manifest.tokenizer.bosTokenId;
    std::vector<int64_t> inputIdsValues = {bosId};
    std::vector<int64_t> inputIdsShape = {1, 1};

    Ort::Value inputIdsTensor = Ort::Value::CreateTensor<int64_t>(
        memoryInfo, inputIdsValues.data(), inputIdsValues.size(), inputIdsShape.data(), inputIdsShape.size());

    const char* preInputNames[] = {"vision_embeds", "input_ids"};
    const char* preOutputNames[] = {"logits",
                                    "present_k0",
                                    "present_k1",
                                    "present_k2",
                                    "present_k3",
                                    "present_k4",
                                    "present_k5",
                                    "present_v0",
                                    "present_v1",
                                    "present_v2",
                                    "present_v3",
                                    "present_v4",
                                    "present_v5"};

    std::vector<Ort::Value> preInputs;
    preInputs.push_back(std::move(visionEmbeds));
    preInputs.push_back(std::move(inputIdsTensor));

    std::vector<Ort::Value> kvCache;

    // We use std::optional to hold the Ort::Value which doesn't have a default constructor in all versions
    std::optional<Ort::Value> logitsTensorOpt;

    try {
        auto preOutputs = m_impl->preSession->Run(
            Ort::RunOptions{nullptr}, preInputNames, preInputs.data(), preInputs.size(), preOutputNames, 13);
        logitsTensorOpt = std::move(preOutputs[0]);
        for (size_t i = 1; i < preOutputs.size(); ++i) {
            kvCache.push_back(std::move(preOutputs[i]));
        }
    } catch (const Ort::Exception& e) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.decode_failed"),
                                              QString::fromStdString(e.what())));
    } catch (const std::bad_alloc&) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.oom"),
                                              QStringLiteral("OOM during prefill inference"),
                                              application::RecoveryAction::Retry,
                                              true));
    }

    const int vocabSize = m_impl->vocab->vocabSize();
    const float rp = m_impl->manifest.generation.repetitionPenalty;
    const int cap = m_impl->manifest.generation.contentRunCap;

    QVector<int> toks;
    std::set<int> seen;
    seen.insert(static_cast<int>(bosId));

    const char* stepInputNames[] = {"input_ids",
                                    "position_ids",
                                    "past_k0",
                                    "past_k1",
                                    "past_k2",
                                    "past_k3",
                                    "past_k4",
                                    "past_k5",
                                    "past_v0",
                                    "past_v1",
                                    "past_v2",
                                    "past_v3",
                                    "past_v4",
                                    "past_v5"};
    const char* stepOutputNames[] = {"logits",
                                     "present_k0",
                                     "present_k1",
                                     "present_k2",
                                     "present_k3",
                                     "present_k4",
                                     "present_k5",
                                     "present_v0",
                                     "present_v1",
                                     "present_v2",
                                     "present_v3",
                                     "present_v4",
                                     "present_v5"};

    auto logitsInfo = logitsTensorOpt->GetTensorTypeAndShapeInfo();
    const auto logitsShape = logitsInfo.GetShape();
    if (logitsShape.size() < 3 || logitsShape[1] <= 0 || logitsShape[2] < vocabSize) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Model,
                                              QStringLiteral("model.incompatible"),
                                              QStringLiteral("Decoder logits do not match the tokenizer vocabulary")));
    }
    const int64_t logitsSequenceLength = logitsShape[1];
    const int64_t logitsVocabularySize = logitsShape[2];
    bool isFirstStep = true;

    bool reachedEos = false;

    for (int step = 0; step < m_impl->manifest.generation.maxLength; ++step) {
        if (context.stopToken.stop_requested()) {
            return std::unexpected<application::ApplicationError>(
                application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                  QStringLiteral("cancelled.user_requested"),
                                                  QStringLiteral("OCR cancelled during decoding")));
        }

        float* logitsData = logitsTensorOpt->GetTensorMutableData<float>();
        if (isFirstStep) {
            logitsData += (logitsSequenceLength - 1) * logitsVocabularySize;
            isFirstStep = false;
        }

        if (rp > 1.0f) {
            for (int id : seen) {
                if (id >= 0 && id < vocabSize) {
                    float val = logitsData[id];
                    logitsData[id] = (val < 0.0f) ? (val * rp) : (val / rp);
                }
            }
        }

        if (cap > 0 && !toks.isEmpty()) {
            int lastId = toks.last();
            if (m_impl->vocab->contentIds().contains(lastId)) {
                int run = 0;
                for (auto it = toks.rbegin(); it != toks.rend(); ++it) {
                    if (*it == lastId)
                        run++;
                    else
                        break;
                }
                if (run >= cap) {
                    logitsData[lastId] = -std::numeric_limits<float>::infinity();
                }
            }
        }

        int nextToken = 0;
        float maxLogit = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < vocabSize; ++i) {
            if (logitsData[i] > maxLogit) {
                maxLogit = logitsData[i];
                nextToken = i;
            }
        }

        if (nextToken == m_impl->manifest.tokenizer.eosTokenId) {
            reachedEos = true;
            break;
        }

        toks.append(nextToken);
        seen.insert(nextToken);

        if (toks.size() >= m_impl->manifest.generation.maxLength) {
            return std::unexpected<application::ApplicationError>(
                application::makeApplicationError(application::ErrorCategory::Inference,
                                                  QStringLiteral("inference.decode_limit"),
                                                  QStringLiteral("Max length reached without EOS")));
        }

        inputIdsValues[0] = nextToken;
        std::vector<int64_t> posIdsValues = {decoderPosition};
        std::vector<int64_t> posIdsShape = {1, 1};

        inputIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, inputIdsValues.data(), inputIdsValues.size(), inputIdsShape.data(), inputIdsShape.size());
        Ort::Value posIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memoryInfo, posIdsValues.data(), posIdsValues.size(), posIdsShape.data(), posIdsShape.size());

        std::vector<Ort::Value> stepInputs;
        stepInputs.push_back(std::move(inputIdsTensor));
        stepInputs.push_back(std::move(posIdsTensor));
        for (auto& kv : kvCache) {
            stepInputs.push_back(std::move(kv));
        }
        kvCache.clear();

        try {
            auto stepOutputs = m_impl->stepSession->Run(
                Ort::RunOptions{nullptr}, stepInputNames, stepInputs.data(), stepInputs.size(), stepOutputNames, 13);
            logitsTensorOpt = std::move(stepOutputs[0]);
            for (size_t i = 1; i < stepOutputs.size(); ++i) {
                kvCache.push_back(std::move(stepOutputs[i]));
            }
        } catch (const Ort::Exception& e) {
            return std::unexpected<application::ApplicationError>(
                application::makeApplicationError(application::ErrorCategory::Inference,
                                                  QStringLiteral("inference.decode_failed"),
                                                  QString::fromStdString(e.what())));
        } catch (const std::bad_alloc&) {
            return std::unexpected<application::ApplicationError>(
                application::makeApplicationError(application::ErrorCategory::Inference,
                                                  QStringLiteral("inference.oom"),
                                                  QStringLiteral("OOM during step inference"),
                                                  application::RecoveryAction::Retry,
                                                  true));
        }

        decoderPosition++;
    }

    if (!reachedEos) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.decode_limit"),
                                              QStringLiteral("Max length reached without EOS")));
    }

    QString decoded = m_impl->vocab->decode(toks);
    if (decoded.isEmpty()) {
        return std::unexpected<application::ApplicationError>(
            application::makeApplicationError(application::ErrorCategory::Inference,
                                              QStringLiteral("inference.empty_result"),
                                              QStringLiteral("Decoded text is empty")));
    }

    if (m_impl->manifest.postprocess != QStringLiteral("baberu-v1")) {
        return std::unexpected<application::ApplicationError>(application::makeApplicationError(
            application::ErrorCategory::Model,
            QStringLiteral("model.incompatible"),
            QStringLiteral("Unsupported postprocess: ") + m_impl->manifest.postprocess));
    }

    application::OcrResponse response;
    response.regionId = request.regionId;
    response.text = decoded;
    response.confidence = 0.0;

    progress.setValue(1);
    progress.setText(QStringLiteral("OCR complete"));
    return response;
}

} // namespace manga::services
