#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/ports/IOcrEngine.h"

#include <QFuture>
#include <QString>

#include <memory>

// Ort::Env is forward declared so this header stays free of the ONNX Runtime API.
namespace Ort {
struct Env;
}

namespace manga::application {
class TaskProgress;
}

namespace manga::services {

class BoundedTaskExecutor;

class OnnxMangaOcrEngine final : public application::IOcrEngine {
public:
    OnnxMangaOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir);
    OnnxMangaOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir);
    ~OnnxMangaOcrEngine() override;

    [[nodiscard]] QFuture<application::Result<application::OcrResponse>>
    recognize(application::OcrRequest request, application::OperationContext context) override;

    void release() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    [[nodiscard]] application::Result<application::OcrResponse>
    runInference(const application::OcrRequest& request,
                 const application::OperationContext& context,
                 application::TaskProgress& progress);

    [[nodiscard]] application::Result<void> ensureLoaded();
};

} // namespace manga::services
