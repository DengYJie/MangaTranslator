#include "services/ocr/OcrFactory.h"

#include "services/execution/BoundedTaskExecutor.h"
#include "services/ocr/OnnxMangaOcrEngine.h"

#include <onnxruntime_cxx_api.h>

namespace manga::services {

std::unique_ptr<application::IOcrEngine> createOcrEngine(BoundedTaskExecutor* executor, QString modelDirectory) {
    auto env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MangaTranslator");
    return std::make_unique<OnnxMangaOcrEngine>(std::move(env), executor, std::move(modelDirectory));
}

} // namespace manga::services
