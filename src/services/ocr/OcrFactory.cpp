#include "services/ocr/OcrFactory.h"

#include "services/execution/BoundedTaskExecutor.h"
#include "services/ocr/CompositeOcrEngine.h"

#include <onnxruntime_cxx_api.h>

namespace manga::services {

void OrtEnvDeleter::operator()(Ort::Env* env) const {
    delete env;
}

UniqueOrtEnv createOcrEnv() {
    return UniqueOrtEnv(new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "MangaTranslator"));
}

std::unique_ptr<application::IOcrEngine>
createOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDirectory) {
    return std::make_unique<CompositeOcrEngine>(env, executor, std::move(modelDirectory));
}

std::unique_ptr<application::IOcrEngine> createOcrEngine(BoundedTaskExecutor* executor, QString modelDirectory) {
    auto env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MangaTranslator");
    return std::make_unique<CompositeOcrEngine>(std::move(env), executor, std::move(modelDirectory));
}

} // namespace manga::services
