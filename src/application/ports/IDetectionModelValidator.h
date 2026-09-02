#pragma once

#include "application/dto/DetectionModelDto.h"
#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::application {

/// Validates that a directory holds a usable detection model bundle without loading weights.
class IDetectionModelValidator {
public:
    virtual ~IDetectionModelValidator() = default;

    /// Reads and checks the bundle description on the calling thread and blocks until that read
    /// finishes, so a caller on a UI thread pays the disk wait itself. The directory reference is
    /// only used during the call, and a valid result copies out the identity and runtime fields the
    /// status carries. Weights are never opened, so a valid result only proves the manifest and the
    /// model file are present and shaped correctly, and a later load can still fail.
    ///
    /// There is no cancellation channel because the call is synchronous. An absent directory, an
    /// absent or unreadable bundle description, and an absent or unsafe model file report
    /// model.missing, while a malformed or unusable description reports model.incompatible, both with
    /// category Model and a locate model recovery hint.
    [[nodiscard]] virtual Result<DetectionModelStatus> validate(const QString& modelDirectory) = 0;
};

} // namespace manga::application
