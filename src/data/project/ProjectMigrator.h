#pragma once

#include "domain/validation/ValidationResult.h"

#include <QJsonObject>

#include <expected>

namespace manga::data {

/// Migrator for upgrading older project.json schemas and folder formats step-by-step.
class ProjectMigrator {
public:
    /// Validates version fields and applies step-by-step migrations to the raw JSON object.
    [[nodiscard]] static std::expected<QJsonObject, domain::ValidationError> migrateIfNeeded(QJsonObject rootObj);
};

} // namespace manga::data
