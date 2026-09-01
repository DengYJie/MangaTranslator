#pragma once

#include "application/ports/IOcrEngine.h"

#include <QString>

#include <memory>

namespace Ort {
struct Env;
}

namespace manga::services {
class BoundedTaskExecutor;

struct OrtEnvDeleter {
    void operator()(Ort::Env* env) const;
};
using UniqueOrtEnv = std::unique_ptr<Ort::Env, OrtEnvDeleter>;

/// Creates the process-wide ORT environment while keeping ONNX Runtime headers confined to services.
[[nodiscard]] UniqueOrtEnv createOcrEnv();

/// Constructs the production OCR adapter using an external Ort::Env while keeping ONNX Runtime confined to services.
[[nodiscard]] std::unique_ptr<application::IOcrEngine>
createOcrEngine(Ort::Env* env, BoundedTaskExecutor* executor, QString modelDirectory);

/// Constructs the production OCR adapter while keeping ONNX Runtime confined to services.
[[nodiscard]] std::unique_ptr<application::IOcrEngine> createOcrEngine(BoundedTaskExecutor* executor,
                                                                       QString modelDirectory);
} // namespace manga::services
