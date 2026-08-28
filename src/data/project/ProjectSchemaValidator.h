#pragma once

#include "domain/validation/ValidationResult.h"

#include <QJsonObject>

#include <expected>

namespace manga::data {

/// Strict structural validator for project.json, implementing JSON Schema Draft 2020-12 rules.
///
/// Two-phase design to support correct migration pipeline:
///   1. validateEnvelope() . lightweight check: required version fields exist and are valid
///                            integers; called BEFORE migration.
///   2. validateFull()     . complete structural check: all fields, types, enums,
///                            additionalProperties:false; called AFTER migration.
class ProjectSchemaValidator {
public:
    /// Phase 1. Pre-migration envelope check.
    /// Only verifies that schemaVersion and formatVersion exist and are integers >= 1.
    /// No other content checks are performed.
    [[nodiscard]] static std::expected<void, domain::ValidationError> validateEnvelope(const QJsonObject& rootObj);

    /// Phase 2. Full structural validation (post-migration).
    /// Validates the complete object against the current schema specification:
    ///   - required fields, additionalProperties:false, type checks, enum values,
    ///     UUID v4 format, numeric ranges.
    [[nodiscard]] static std::expected<void, domain::ValidationError> validateFull(const QJsonObject& rootObj);
};

} // namespace manga::data
