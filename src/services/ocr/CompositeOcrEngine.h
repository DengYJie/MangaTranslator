#pragma once

#include "application/ports/IOcrEngine.h"

#include <QString>

#include <memory>

namespace Ort {
struct Env;
}

namespace manga::services {

class BoundedTaskExecutor;

class CompositeOcrEngine final : public application::IOcrEngine {
public:
    CompositeOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDir);
    CompositeOcrEngine(std::unique_ptr<Ort::Env> env, BoundedTaskExecutor* executor, QString modelDir);
    ~CompositeOcrEngine() override;

    [[nodiscard]] QFuture<application::Result<application::OcrResponse>>
    recognize(application::OcrRequest request, application::OperationContext context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace manga::services
