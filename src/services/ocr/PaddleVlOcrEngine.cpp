#include "PaddleVlOcrEngine.h"

#include "BpeVocab.h"
#include "PaddleVlPreprocessor.h"
#include "application/operation/TaskProgress.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>
#include <QSemaphore>
#include <QSemaphoreReleaser>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <onnxruntime_cxx_api.h>
#include <optional>
#include <vector>

namespace manga::services {

struct PaddleVlOcrEngine::Impl {
    std::unique_ptr<Ort::Env> ownedEnv;
    Ort::Env* env{nullptr};
    BoundedTaskExecutor* executor{nullptr};
    QString modelDir;

    std::unique_ptr<Ort::Session> visSession;
    std::unique_ptr<Ort::Session> embedSession;
    std::unique_ptr<Ort::Session> decoderSession;

    std::optional<BpeVocab> vocab;

    bool loaded{false};
    int activeInferences{0};
    QMutex loadMutex;
    QSemaphore concurrencyGate{1};

    static constexpr int kNumLayers = 18;
    static constexpr int kHiddenDim = 1024;
    static constexpr int kNumHeads = 2;
    static constexpr int kHeadDim = 128;
};

PaddleVlOcrEngine::PaddleVlOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

PaddleVlOcrEngine::PaddleVlOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->ownedEnv = std::move(env);
    m_impl->env = m_impl->ownedEnv.get();
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
}

PaddleVlOcrEngine::~PaddleVlOcrEngine() = default;

void PaddleVlOcrEngine::release() {
    QMutexLocker locker(&m_impl->loadMutex);
    if (m_impl->activeInferences == 0 && m_impl->loaded) {
        m_impl->visSession.reset();
        m_impl->embedSession.reset();
        m_impl->decoderSession.reset();
        m_impl->vocab.reset();
        m_impl->loaded = false;
        qInfo().noquote() << "[PaddleVlOcrEngine] released sessions from memory";
    }
}

bool PaddleVlOcrEngine::isAvailable() const {
    if (m_impl->modelDir.trimmed().isEmpty()) {
        return false;
    }
    const QDir dir(m_impl->modelDir);
    return (dir.exists(QStringLiteral("onnx/vision_encoder_q8.onnx")) ||
            dir.exists(QStringLiteral("vision_encoder_q8.onnx"))) &&
           (dir.exists(QStringLiteral("onnx/embedding.onnx")) || dir.exists(QStringLiteral("embedding.onnx"))) &&
           (dir.exists(QStringLiteral("onnx/decoder_q8.onnx")) || dir.exists(QStringLiteral("decoder_q8.onnx")));
}

application::Result<void> PaddleVlOcrEngine::ensureLoaded() {
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
    auto resolveFile = [&](const QString& rel1, const QString& rel2) -> QString {
        if (dir.exists(rel1))
            return dir.filePath(rel1);
        if (dir.exists(rel2))
            return dir.filePath(rel2);
        return QString();
    };

    QString visPath =
        resolveFile(QStringLiteral("onnx/vision_encoder_q8.onnx"), QStringLiteral("vision_encoder_q8.onnx"));
    QString embedPath = resolveFile(QStringLiteral("onnx/embedding.onnx"), QStringLiteral("embedding.onnx"));
    QString decPath = resolveFile(QStringLiteral("onnx/decoder_q8.onnx"), QStringLiteral("decoder_q8.onnx"));
    QString tokPath = resolveFile(QStringLiteral("tokenizer.json"), QStringLiteral("tokenizer/vocab.json"));

    if (visPath.isEmpty() || embedPath.isEmpty() || decPath.isEmpty() || tokPath.isEmpty()) {
        qWarning().noquote() << "[PaddleVlOcrEngine] missing required model files in" << m_impl->modelDir;
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.missing"),
                                                                 QStringLiteral("PaddleOCR-VL model files not found"),
                                                                 application::RecoveryAction::LocateModel));
    }

    auto vocabRes = BpeVocab::load(tokPath);
    if (!vocabRes.has_value()) {
        qWarning().noquote() << "[PaddleVlOcrEngine] failed to load tokenizer:" << vocabRes.error().safeMessage;
        return std::unexpected(vocabRes.error());
    }
    m_impl->vocab = std::move(vocabRes.value());

    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetIntraOpNumThreads(2);
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    try {
#ifdef _WIN32
        m_impl->visSession = std::make_unique<Ort::Session>(*m_impl->env, visPath.toStdWString().c_str(), options);
        m_impl->embedSession = std::make_unique<Ort::Session>(*m_impl->env, embedPath.toStdWString().c_str(), options);
        m_impl->decoderSession = std::make_unique<Ort::Session>(*m_impl->env, decPath.toStdWString().c_str(), options);
#else
        m_impl->visSession = std::make_unique<Ort::Session>(*m_impl->env, visPath.toStdString().c_str(), options);
        m_impl->embedSession = std::make_unique<Ort::Session>(*m_impl->env, embedPath.toStdString().c_str(), options);
        m_impl->decoderSession = std::make_unique<Ort::Session>(*m_impl->env, decPath.toStdString().c_str(), options);
#endif
        m_impl->loaded = true;
        qInfo().noquote() << "[PaddleVlOcrEngine] successfully loaded PaddleOCR-VL model from" << m_impl->modelDir;
    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleVlOcrEngine] ONNX session creation exception:" << e.what();
        return std::unexpected(application::makeApplicationError(
            application::ErrorCategory::Model, QStringLiteral("model.load_failed"), QString::fromStdString(e.what())));
    }

    return {};
}

QFuture<application::Result<application::OcrResponse>>
PaddleVlOcrEngine::recognize(application::OcrRequest request, application::OperationContext context) {
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
PaddleVlOcrEngine::runInference(const application::OcrRequest& request,
                                const application::OperationContext& context,
                                application::TaskProgress& progress) {
    progress.setRange(0, 100);
    progress.setValue(0);
    progress.setText(QStringLiteral("Loading PaddleOCR-VL model..."));

    QMutexLocker locker(&m_impl->loadMutex);
    while (!m_impl->loaded) {
        locker.unlock();
        auto loadRes = ensureLoaded();
        if (!loadRes.has_value()) {
            return std::unexpected(loadRes.error());
        }
        locker.relock();
    }
    ++m_impl->activeInferences;
    struct InferenceGuard {
        PaddleVlOcrEngine::Impl* impl;
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
                                                                 QStringLiteral("OCR cancelled before preprocessing")));
    }

    QElapsedTimer timer;
    timer.start();

    // 1. Preprocess image
    progress.setValue(10);
    progress.setText(QStringLiteral("Preprocessing image for Vision-Language model..."));

    auto preResult = PaddleVlPreprocessor::preprocess(request);
    if (!preResult.has_value()) {
        qWarning().noquote() << "[PaddleVlOcrEngine] preprocess failed:" << preResult.error().safeMessage;
        return std::unexpected(preResult.error());
    }
    auto& prepData = preResult.value();

    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // 2. Vision Encoder
    progress.setValue(25);
    progress.setText(QStringLiteral("Running NaViT Vision Encoder..."));

    const std::vector<int64_t> pixelShape = {
        1, prepData.numPatches, 3, PaddleVlPreprocessor::kPatchSize, PaddleVlPreprocessor::kPatchSize};
    Ort::Value pixelTensor = Ort::Value::CreateTensor<float>(
        memInfo, prepData.pixelValues.data(), prepData.pixelValues.size(), pixelShape.data(), pixelShape.size());

    const std::vector<int64_t> gridShape = {1, 3};
    Ort::Value gridTensor = Ort::Value::CreateTensor<int64_t>(
        memInfo, prepData.gridThw.data(), prepData.gridThw.size(), gridShape.data(), gridShape.size());

    const char* visInputNames[] = {"pixel_values", "image_grid_thw"};
    Ort::Value visInputs[] = {std::move(pixelTensor), std::move(gridTensor)};
    const char* visOutputNames[] = {"image_embeds"};

    std::vector<Ort::Value> visOutputs;
    try {
        visOutputs = m_impl->visSession->Run(Ort::RunOptions{nullptr}, visInputNames, visInputs, 2, visOutputNames, 1);
    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleVlOcrEngine] Vision encoder exception:" << e.what();
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.failed"),
                                                                 QString::fromStdString(e.what())));
    }

    if (visOutputs.empty()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.empty_vision"),
                                                                 QStringLiteral("Vision encoder produced no outputs")));
    }

    Ort::Value& imageEmbedsTensor = visOutputs[0];
    const float* imageEmbedsData = imageEmbedsTensor.GetTensorData<float>();
    const int mergedTokens = prepData.mergedTokens;

    // 3. Build Prompt Tokens & Embedding Lookup
    // Prompt format: <|begin_of_sentence|>User: <|IMAGE_START|>{placeholders}<|IMAGE_END|>OCR:\nAssistant:\n
    std::vector<int64_t> promptTokens;
    promptTokens.push_back(100273); // <|begin_of_sentence|>

    // "User: "
    int uId = m_impl->vocab->tokenToId(QStringLiteral("User"));
    if (uId >= 0)
        promptTokens.push_back(uId);
    int colonId = m_impl->vocab->tokenToId(QStringLiteral(":"));
    if (colonId >= 0)
        promptTokens.push_back(colonId);
    int spaceId = m_impl->vocab->tokenToId(QStringLiteral(" "));
    if (spaceId >= 0)
        promptTokens.push_back(spaceId);

    promptTokens.push_back(101305); // <|IMAGE_START|>
    const size_t imagePlaceholderStartIndex = promptTokens.size();
    for (int i = 0; i < mergedTokens; ++i) {
        promptTokens.push_back(100295); // <|IMAGE_PLACEHOLDER|>
    }
    promptTokens.push_back(101306); // <|IMAGE_END|>

    // "OCR:\nAssistant:\n"
    int ocrId = m_impl->vocab->tokenToId(QStringLiteral("OCR"));
    if (ocrId >= 0)
        promptTokens.push_back(ocrId);
    if (colonId >= 0)
        promptTokens.push_back(colonId);
    int nlId = m_impl->vocab->tokenToId(QStringLiteral("<nl>"));
    if (nlId >= 0)
        promptTokens.push_back(nlId);

    int asstId = m_impl->vocab->tokenToId(QStringLiteral("Assistant"));
    if (asstId >= 0)
        promptTokens.push_back(asstId);
    if (colonId >= 0)
        promptTokens.push_back(colonId);
    if (nlId >= 0)
        promptTokens.push_back(nlId);

    const int64_t seqLen = static_cast<int64_t>(promptTokens.size());
    const std::vector<int64_t> promptShape = {1, seqLen};
    Ort::Value promptTensor = Ort::Value::CreateTensor<int64_t>(
        memInfo, promptTokens.data(), promptTokens.size(), promptShape.data(), promptShape.size());

    const char* embedInputNames[] = {"input_ids"};
    Ort::Value embedInputs[] = {std::move(promptTensor)};
    const char* embedOutputNames[] = {"embeddings"};

    std::vector<Ort::Value> embedOutputs;
    try {
        embedOutputs =
            m_impl->embedSession->Run(Ort::RunOptions{nullptr}, embedInputNames, embedInputs, 1, embedOutputNames, 1);
    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleVlOcrEngine] Embedding lookup exception:" << e.what();
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.failed"),
                                                                 QString::fromStdString(e.what())));
    }

    Ort::Value& embeddingsTensor = embedOutputs[0];
    float* embeddingsData = embeddingsTensor.GetTensorMutableData<float>();

    // Splicing: replace <|IMAGE_PLACEHOLDER|> embeddings with image_embeds
    constexpr int hiddenDim = Impl::kHiddenDim; // 1024
    int64_t totalElements = imageEmbedsTensor.GetTensorTypeAndShapeInfo().GetElementCount();
    if (totalElements < mergedTokens * hiddenDim) {
        qWarning().noquote() << "[PaddleVlOcrEngine] imageEmbeds mismatch: expected" << mergedTokens * hiddenDim
                             << "but got" << totalElements;
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.failed"),
                                                                 QStringLiteral("Vision embeddings tensor too small")));
    }

    float* dstSlice = embeddingsData + imagePlaceholderStartIndex * hiddenDim;
    std::memcpy(dstSlice, imageEmbedsData, mergedTokens * hiddenDim * sizeof(float));

    // 4. Decoder Prefill
    progress.setValue(45);
    progress.setText(QStringLiteral("Decoding visual tokens..."));

    // Prepare attention mask: [1, seqLen] all 1.0f (or int64_t depending on onnx)
    std::vector<int64_t> attnMask(seqLen, 1);
    const std::vector<int64_t> maskShape = {1, seqLen};
    Ort::Value maskTensor = Ort::Value::CreateTensor<int64_t>(
        memInfo, attnMask.data(), attnMask.size(), maskShape.data(), maskShape.size());

    // Prepare 36 empty past KV tensors: [1, 2, 0, 128]
    const std::vector<int64_t> emptyKvShape = {1, 2, 0, 128};
    std::vector<float> emptyKvData(1, 0.0f);

    std::vector<std::string> pastNamesStr;
    std::vector<const char*> decoderInputNames;
    decoderInputNames.push_back("inputs_embeds");
    decoderInputNames.push_back("attention_mask");

    for (int i = 0; i < Impl::kNumLayers; ++i) {
        pastNamesStr.push_back("past_key_values." + std::to_string(i) + ".key");
        pastNamesStr.push_back("past_key_values." + std::to_string(i) + ".value");
    }
    for (const auto& name : pastNamesStr) {
        decoderInputNames.push_back(name.c_str());
    }

    std::vector<std::string> presentNamesStr = {"logits"};
    std::vector<const char*> decoderOutputNames;
    for (int i = 0; i < Impl::kNumLayers; ++i) {
        presentNamesStr.push_back("present." + std::to_string(i) + ".key");
        presentNamesStr.push_back("present." + std::to_string(i) + ".value");
    }
    for (const auto& name : presentNamesStr) {
        decoderOutputNames.push_back(name.c_str());
    }

    std::vector<Ort::Value> decoderInputs;
    decoderInputs.push_back(std::move(embeddingsTensor));
    decoderInputs.push_back(std::move(maskTensor));

    for (int i = 0; i < Impl::kNumLayers * 2; ++i) {
        decoderInputs.push_back(
            Ort::Value::CreateTensor<float>(memInfo, emptyKvData.data(), 0, emptyKvShape.data(), emptyKvShape.size()));
    }

    std::vector<Ort::Value> decoderOutputs;
    try {
        decoderOutputs = m_impl->decoderSession->Run(Ort::RunOptions{nullptr},
                                                     decoderInputNames.data(),
                                                     decoderInputs.data(),
                                                     decoderInputs.size(),
                                                     decoderOutputNames.data(),
                                                     decoderOutputNames.size());
    } catch (const Ort::Exception& e) {
        qWarning().noquote() << "[PaddleVlOcrEngine] Decoder prefill exception:" << e.what();
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                 QStringLiteral("inference.failed"),
                                                                 QString::fromStdString(e.what())));
    }

    // 5. Autoregressive Loop
    Ort::Value& prefillLogitsTensor = decoderOutputs[0];
    auto logitsShape = prefillLogitsTensor.GetTensorTypeAndShapeInfo().GetShape();
    const int64_t vocabSize = logitsShape[2];
    const int64_t outSeqLen = logitsShape[1];
    const float* prefillLogits = prefillLogitsTensor.GetTensorData<float>();

    // Last token logits
    const float* stepLogits = prefillLogits + (outSeqLen - 1) * vocabSize;
    std::vector<float> currentLogits(stepLogits, stepLogits + vocabSize);

    std::vector<Ort::Value> currentKv;
    for (size_t i = 1; i < decoderOutputs.size(); ++i) {
        currentKv.push_back(std::move(decoderOutputs[i]));
    }

    QVector<int> generatedTokens;
    constexpr int maxNewTokens = 256;
    constexpr float repetitionPenalty = 1.2f;
    int64_t totalLen = seqLen;

    for (int step = 0; step < maxNewTokens; ++step) {
        if (context.stopToken.stop_requested()) {
            break;
        }

        // Apply repetition penalty
        for (int tok : generatedTokens) {
            if (tok >= 0 && tok < static_cast<int>(currentLogits.size())) {
                float v = currentLogits[tok];
                currentLogits[tok] = (v > 0.0f) ? (v / repetitionPenalty) : (v * repetitionPenalty);
            }
        }

        // Greedy argmax
        int bestTok = 0;
        float maxVal = currentLogits[0];
        for (int i = 1; i < static_cast<int>(currentLogits.size()); ++i) {
            if (currentLogits[i] > maxVal) {
                maxVal = currentLogits[i];
                bestTok = i;
            }
        }

        // Check EOS (100272 <|end_of_sentence|> or 2 </s>)
        if (bestTok == 100272 || bestTok == 2 || bestTok == m_impl->vocab->eosId()) {
            break;
        }

        generatedTokens.append(bestTok);
        totalLen += 1;

        // Embedding lookup for single token
        int64_t nextTok64 = bestTok;
        const std::vector<int64_t> singleShape = {1, 1};
        Ort::Value nextTokTensor =
            Ort::Value::CreateTensor<int64_t>(memInfo, &nextTok64, 1, singleShape.data(), singleShape.size());

        Ort::Value nextEmbedInputs[] = {std::move(nextTokTensor)};
        auto nextEmbedOutputs = m_impl->embedSession->Run(
            Ort::RunOptions{nullptr}, embedInputNames, nextEmbedInputs, 1, embedOutputNames, 1);
        Ort::Value nextEmbeds = std::move(nextEmbedOutputs[0]);

        // Attention mask for step: [1, totalLen]
        std::vector<int64_t> stepMask(totalLen, 1);
        const std::vector<int64_t> stepMaskShape = {1, totalLen};
        Ort::Value stepMaskTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, stepMask.data(), stepMask.size(), stepMaskShape.data(), stepMaskShape.size());

        std::vector<Ort::Value> stepInputs;
        stepInputs.push_back(std::move(nextEmbeds));
        stepInputs.push_back(std::move(stepMaskTensor));
        for (auto& kv : currentKv) {
            stepInputs.push_back(std::move(kv));
        }

        try {
            auto stepOutputs = m_impl->decoderSession->Run(Ort::RunOptions{nullptr},
                                                           decoderInputNames.data(),
                                                           stepInputs.data(),
                                                           stepInputs.size(),
                                                           decoderOutputNames.data(),
                                                           decoderOutputNames.size());

            auto stepShape = stepOutputs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int64_t stepOutSeqLen = stepShape[1];
            const float* outLogits = stepOutputs[0].GetTensorData<float>() + (stepOutSeqLen - 1) * vocabSize;
            currentLogits.assign(outLogits, outLogits + vocabSize);

            currentKv.clear();
            for (size_t i = 1; i < stepOutputs.size(); ++i) {
                currentKv.push_back(std::move(stepOutputs[i]));
            }
        } catch (const Ort::Exception& e) {
            qWarning().noquote() << "[PaddleVlOcrEngine] Decoder step exception:" << e.what();
            break;
        }

        if (step % 10 == 0) {
            int pct = 45 + static_cast<int>((static_cast<double>(step) / maxNewTokens) * 50.0);
            progress.setValue(std::min(95, pct));
        }
    }

    QString decodedText = m_impl->vocab->decode(generatedTokens);

    qInfo().noquote() << "[PaddleVlOcrEngine] complete region=" << QString::fromStdString(request.regionId.toString())
                      << "text=\"" << decodedText << "\" elapsed=" << timer.elapsed() << "ms";

    application::OcrResponse response;
    response.regionId = request.regionId;
    response.text = decodedText;
    response.confidence = 0.95;

    progress.setValue(100);
    progress.setText(QStringLiteral("PaddleOCR-VL complete"));
    return response;
}

} // namespace manga::services
