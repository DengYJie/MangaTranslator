#pragma once

#include "application/dto/TextDetectionDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/ports/ITextDetector.h"

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

class OnnxKoharuLayoutDetector final : public application::ITextDetector {
public:
    OnnxKoharuLayoutDetector(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir);
    OnnxKoharuLayoutDetector(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir);
    ~OnnxKoharuLayoutDetector() override;

    [[nodiscard]] QFuture<application::Result<application::TextDetectionResponse>>
    detect(application::TextDetectionRequest request, application::OperationContext context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    [[nodiscard]] application::Result<application::TextDetectionResponse>
    runInference(const application::TextDetectionRequest& request,
                 const application::OperationContext& context,
                 application::TaskProgress& progress);

    [[nodiscard]] application::Result<void> ensureLoaded();
};

} // namespace manga::services
