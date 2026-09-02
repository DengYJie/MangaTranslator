#include "CompositeOcrEngine.h"

#include "OnnxMangaOcrEngine.h"
#include "PaddleOcrEngine.h"
#include "domain/model/LanguageCode.h"

#include <QDebug>

#include <onnxruntime_cxx_api.h>

namespace manga::services {

struct CompositeOcrEngine::Impl {
    std::unique_ptr<Ort::Env> ownedEnv;
    Ort::Env* env{nullptr};
    BoundedTaskExecutor* executor{nullptr};
    QString modelDir;

    std::unique_ptr<OnnxMangaOcrEngine> baberuEngine;
    std::unique_ptr<PaddleOcrEngine> paddleEngine;
};

CompositeOcrEngine::CompositeOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->env = env;
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
    m_impl->baberuEngine = std::make_unique<OnnxMangaOcrEngine>(m_impl->env, m_impl->executor, m_impl->modelDir);
    m_impl->paddleEngine = std::make_unique<PaddleOcrEngine>(m_impl->env, m_impl->executor, m_impl->modelDir);
}

CompositeOcrEngine::CompositeOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->ownedEnv = std::move(env);
    m_impl->env = m_impl->ownedEnv.get();
    m_impl->executor = executor;
    m_impl->modelDir = std::move(modelDir);
    m_impl->baberuEngine = std::make_unique<OnnxMangaOcrEngine>(m_impl->env, m_impl->executor, m_impl->modelDir);
    m_impl->paddleEngine = std::make_unique<PaddleOcrEngine>(m_impl->env, m_impl->executor, m_impl->modelDir);
}

CompositeOcrEngine::~CompositeOcrEngine() = default;

QFuture<application::Result<application::OcrResponse>>
CompositeOcrEngine::recognize(application::OcrRequest request, application::OperationContext context) {
    const std::string lang = request.language.toString();

    qInfo().noquote() << "[CompositeOcrEngine] routing request lang=" << QString::fromStdString(lang)
                      << "paddleAvailable=" << (m_impl->paddleEngine && m_impl->paddleEngine->isAvailable());

    // 1. Japanese manga text -> prefer Baberu OCR
    if (lang == "ja" || lang == "jpn" || lang == "Japanese") {
        return m_impl->baberuEngine->recognize(std::move(request), std::move(context));
    }

    // 2. Chinese / English / Korean -> prefer PaddleOCR
    if (lang == "zh" || lang == "zh-CN" || lang == "zh-TW" || lang == "en" || lang == "ko" || lang == "kor") {
        if (m_impl->paddleEngine && m_impl->paddleEngine->isAvailable()) {
            return m_impl->paddleEngine->recognize(std::move(request), std::move(context));
        }
    }

    // 3. Auto / Fallback:
    if (m_impl->paddleEngine && m_impl->paddleEngine->isAvailable()) {
        return m_impl->paddleEngine->recognize(std::move(request), std::move(context));
    }

    return m_impl->baberuEngine->recognize(std::move(request), std::move(context));
}

} // namespace manga::services
