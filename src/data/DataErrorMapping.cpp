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
    switch (fileError) {
        case QFileDevice::PermissionsError:
            code = QStringLiteral("persistence.permission_denied");
            break;
        case QFileDevice::ResourceError:
            // Qt reports ENOSPC / ERROR_DISK_FULL as ResourceError.
            code = QStringLiteral("persistence.disk_full");
            break;
        default:
            code = QStringLiteral("persistence.io");
            break;
    }
    return application::makeApplicationError(
        application::ErrorCategory::Persistence, code, QStringLiteral("A file operation failed"));
}

application::ApplicationError fileNotFoundError() {
    return application::makeApplicationError(application::ErrorCategory::Persistence,
                                             QStringLiteral("persistence.not_found"),
                                             QStringLiteral("The file or directory was not found"));
}

} // namespace manga::data
