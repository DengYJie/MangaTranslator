#include "data/DataErrorMapping.h"

namespace manga::data {

application::ApplicationError toApplicationError(const domain::ValidationError& error) {
    using domain::ProjectErrorCode;
    QString code;
    switch (error.code) {
        case ProjectErrorCode::UnsupportedSchemaVersion:
            code = QStringLiteral("project.unsupported_schema_version");
            break;
        case ProjectErrorCode::UnsupportedFormatVersion:
            code = QStringLiteral("project.unsupported_format_version");
            break;
        case ProjectErrorCode::InvalidStructure:
            code = QStringLiteral("project.invalid_structure");
            break;
        case ProjectErrorCode::MissingAsset:
            code = QStringLiteral("project.missing_asset");
            break;
        case ProjectErrorCode::ImageDimensionMismatch:
            code = QStringLiteral("project.image_dimension_mismatch");
            break;
        default:
            code = QStringLiteral("project.invalid_semantic");
            break;
    }
    auto applicationError = application::makeApplicationError(
        application::ErrorCategory::Project, code, QStringLiteral("Project data failed validation"));
    applicationError.diagnostic = QString::fromStdString(error.toString());
    return applicationError;
}

application::ApplicationError toPersistenceError(QFileDevice::FileError fileError) {
    QString code;
    QString safeMessage;
    application::RecoveryAction recovery = application::RecoveryAction::None;
    bool retryable = false;
    switch (fileError) {
        case QFileDevice::PermissionsError:
            code = QStringLiteral("persistence.permission_denied");
            safeMessage = QStringLiteral("Permission denied accessing the project file or directory");
            recovery = application::RecoveryAction::ChoosePath;
            break;
        case QFileDevice::ResourceError:
            // Qt reports ENOSPC / ERROR_DISK_FULL as ResourceError.
            code = QStringLiteral("persistence.disk_full");
            safeMessage = QStringLiteral("Not enough disk space for this operation");
            recovery = application::RecoveryAction::FreeDiskSpace;
            break;
        default:
            code = QStringLiteral("persistence.io");
            safeMessage = QStringLiteral("A file I/O operation failed");
            recovery = application::RecoveryAction::Retry;
            retryable = true;
            break;
    }
    auto applicationError = application::makeApplicationError(
        application::ErrorCategory::Persistence, code, safeMessage, recovery, retryable);
    applicationError.diagnostic = QStringLiteral("FileError(%1)").arg(static_cast<int>(fileError));
    return applicationError;
}

application::ApplicationError toProjectLockError(QLockFile::LockError error) {
    application::ApplicationError appError;
    switch (error) {
        case QLockFile::LockFailedError:
            appError =
                application::makeApplicationError(application::ErrorCategory::Persistence,
                                                  QStringLiteral("persistence.locked"),
                                                  QStringLiteral("The project is already open in another instance"),
                                                  application::RecoveryAction::Retry,
                                                  /*retryable=*/true);
            break;
        case QLockFile::PermissionError:
            appError = application::makeApplicationError(
                application::ErrorCategory::Persistence,
                QStringLiteral("persistence.read_only"),
                QStringLiteral("The project directory is read-only and cannot be opened for editing"),
                application::RecoveryAction::ChoosePath,
                /*retryable=*/false);
            break;
        case QLockFile::NoError:
        case QLockFile::UnknownError:
        default:
            appError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                         QStringLiteral("persistence.io"),
                                                         QStringLiteral("Cannot access the project directory"),
                                                         application::RecoveryAction::Retry,
                                                         /*retryable=*/true);
            appError.diagnostic = QStringLiteral("QLockFile::LockError(%1)").arg(static_cast<int>(error));
            break;
    }
    return appError;
}

application::ApplicationError fileNotFoundError() {
    return application::makeApplicationError(application::ErrorCategory::Persistence,
                                             QStringLiteral("persistence.not_found"),
                                             QStringLiteral("The file or directory was not found"),
                                             application::RecoveryAction::ChoosePath);
}

application::ApplicationError notAProjectError() {
    return application::makeApplicationError(application::ErrorCategory::Project,
                                             QStringLiteral("project.not_a_project"),
                                             QStringLiteral("The selected directory is not a MangaTranslator project"),
                                             application::RecoveryAction::ChoosePath);
}

} // namespace manga::data
