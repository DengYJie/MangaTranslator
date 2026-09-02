#pragma once

#include "domain/validation/ValidationResult.h"

#include <QJsonObject>

#include <expected>

namespace manga::data {

/// Strict structural validator for project.json implementing JSON Schema Draft 2020-12 rules.
///
/// The two entry points sit on either side of migration. A document written by an older schema is
/// judged only by its version fields before ProjectMigrator rewrites it, and by every current rule
/// afterwards.
class ProjectSchemaValidator {
public:
    /// Confirms schemaVersion and formatVersion are present and hold positive integers.
    /// Performs no content check, so an unmigrated document can still reach ProjectMigrator.
    [[nodiscard]] static std::expected<void, domain::ValidationError> validateEnvelope(const QJsonObject& rootObj);

    /// Confirms the whole document against the current schema: required fields,
    /// additionalProperties false, types, enum values, UUID v4 format and numeric ranges.
    /// The enum values it accepts are the on-disk spellings produced by ProjectSerializer.
    [[nodiscard]] static std::expected<void, domain::ValidationError> validateFull(const QJsonObject& rootObj);
};

} // namespace manga::data
