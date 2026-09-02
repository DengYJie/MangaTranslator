#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/ports/IOcrEngine.h"

#include <QFuture>
#include <QString>

#include <memory>

namespace Ort {
struct Env;
}

namespace manga::application {
class TaskProgress;
}

namespace manga::services {

class BoundedTaskExecutor;

class PaddleOcrEngine final : public application::IOcrEngine {
public:
    PaddleOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir);
    PaddleOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir);
    ~PaddleOcrEngine() override;

    [[nodiscard]] QFuture<application::Result<application::OcrResponse>>
    recognize(application::OcrRequest request, application::OperationContext context) override;

    [[nodiscard]] bool isAvailable() const;
    void release() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    [[nodiscard]] application::Result<void> ensureLoaded();
    [[nodiscard]] application::Result<application::OcrResponse>
    runInference(const application::OcrRequest& request,
                 const application::OperationContext& context,
                 application::TaskProgress& progress);
};

} // namespace manga::services
