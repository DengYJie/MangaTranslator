#pragma once

#include <expected>
#include <string>
#include <vector>

namespace manga::domain {

enum class ProjectErrorCode {
    UnsupportedSchemaVersion,
    UnsupportedFormatVersion,
    InvalidStructure,
    InvalidSemanticData,
    MissingAsset,
    ImageDimensionMismatch,
    OutOfBoundsGeometry,
    InvalidId,
    DuplicateId,
    DuplicateOrderIndex,
    InvalidCandidateSelection,
    InvalidLayout,
    InvalidClearSettings,
    // Edit operation errors
    EditNotFound,
    EditGeometryRejected,
    EditOrderRejected,
    EditLayoutRejected,
    EditTranslationRejected,
};

/// Structured diagnostic failure message.
struct ValidationError {
    ProjectErrorCode code{ProjectErrorCode::InvalidSemanticData};
    std::string message;
    std::string path;
    std::string entityId;

    [[nodiscard]] std::string toString() const {
        std::string result = message;
        if (!path.empty()) {
            result += " at " + path;
        }
        if (!entityId.empty()) {
            result += " (ID: " + entityId + ")";
        }
        return result;
    }
};

using ValidationResult = std::expected<void, std::vector<ValidationError>>;

/// Error type for project edit operations (transactional APIs).
using ProjectEditError = ValidationError;

} // namespace manga::domain
