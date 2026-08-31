#pragma once

#include "application/error/ApplicationError.h"
#include "domain/validation/ValidationResult.h"

#include <QFileDevice>
#include <QLockFile>

namespace manga::data {

/// Maps data-layer validation failures to stable Application error codes at the port boundary.
[[nodiscard]] application::ApplicationError toApplicationError(const domain::ValidationError& error);

/// Maps Qt file-device failures to stable persistence error codes.
[[nodiscard]] application::ApplicationError toPersistenceError(QFileDevice::FileError fileError);

/// Maps Qt lock-file failures to stable persistence error codes.
[[nodiscard]] application::ApplicationError toProjectLockError(QLockFile::LockError error);

/// Creates a persistence.not_found error for missing files and directories.
[[nodiscard]] application::ApplicationError fileNotFoundError();

/// Creates a project.not_a_project error for existing directories missing project.json.
[[nodiscard]] application::ApplicationError notAProjectError();

} // namespace manga::data
