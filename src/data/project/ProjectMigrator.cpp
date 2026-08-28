#include "data/project/ProjectMigrator.h"

#include "domain/model/ComicProject.h"

#include <format>

namespace manga::data {

std::expected<QJsonObject, domain::ValidationError> ProjectMigrator::migrateIfNeeded(QJsonObject rootObj) {
    if (!rootObj.contains("schemaVersion") || !rootObj["schemaVersion"].isDouble()) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "Missing or invalid 'schemaVersion' field in project.json",
                                    .path = "schemaVersion"});
    }

    if (!rootObj.contains("formatVersion") || !rootObj["formatVersion"].isDouble()) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "Missing or invalid 'formatVersion' field in project.json",
                                    .path = "formatVersion"});
    }

    const int schemaVer = rootObj["schemaVersion"].toInt();
    const int formatVer = rootObj["formatVersion"].toInt();

    if (schemaVer > domain::ComicProject::kCurrentSchemaVersion) {
        return std::unexpected(domain::ValidationError{
            .code = domain::ProjectErrorCode::UnsupportedSchemaVersion,
            .message = std::format("Unsupported schema version: {}, maximum supported version is {}",
                                   schemaVer,
                                   domain::ComicProject::kCurrentSchemaVersion),
            .path = "schemaVersion"});
    }

    if (formatVer > domain::ComicProject::kCurrentFormatVersion) {
        return std::unexpected(domain::ValidationError{
            .code = domain::ProjectErrorCode::UnsupportedFormatVersion,
            .message = std::format("Unsupported format version: {}, maximum supported version is {}",
                                   formatVer,
                                   domain::ComicProject::kCurrentFormatVersion),
            .path = "formatVersion"});
    }

    // Step-by-step migrations will be added here as future schema versions emerge:
    // while (schemaVer < domain::ComicProject::kCurrentSchemaVersion) { ... }

    return rootObj;
}

} // namespace manga::data
