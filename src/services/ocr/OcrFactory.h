#pragma once

#include "application/ports/IOcrEngine.h"

#include <QString>

#include <memory>

namespace manga::services {
class BoundedTaskExecutor;

/// Constructs the production OCR adapter while keeping ONNX Runtime confined to services.
[[nodiscard]] std::unique_ptr<application::IOcrEngine> createOcrEngine(BoundedTaskExecutor* executor,
                                                                       QString modelDirectory);
} // namespace manga::services
