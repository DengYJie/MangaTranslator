#pragma once

#include "application/error/ApplicationError.h"
#include "domain/validation/ValidationResult.h"

#include <QFileDevice>

namespace manga::data {

/// Maps data-layer validation failures to stable Application error codes at the port boundary.
[[nodiscard]] application::ApplicationError toApplicationError(const domain::ValidationError& error);

/// Maps Qt file-device failures to stable persistence error codes.
[[nodiscard]] application::ApplicationError toPersistenceError(QFileDevice::FileError fileError);

/// Creates a persistence.not_found error for missing files and directories.
[[nodiscard]] application::ApplicationError fileNotFoundError();

} // namespace manga::data
