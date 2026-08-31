#pragma once

#include "application/ports/IOcrModelValidator.h"

namespace manga::services {

/// Port adapter over Baberu bundle validation. Validation is file I/O bound and never creates an
/// ONNX session, so callers may run it on any worker thread.
class OcrModelValidator final : public application::IOcrModelValidator {
public:
    [[nodiscard]] application::Result<application::OcrModelStatus> validate(const QString& modelDirectory) override;
};

} // namespace manga::services
