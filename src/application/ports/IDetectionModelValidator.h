#pragma once

#include "application/dto/DetectionModelDto.h"
#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::application {

/// Validates that a directory holds a usable detection model bundle without loading weights.
class IDetectionModelValidator {
public:
    virtual ~IDetectionModelValidator() = default;

    [[nodiscard]] virtual Result<DetectionModelStatus> validate(const QString& modelDirectory) = 0;
};

} // namespace manga::application
