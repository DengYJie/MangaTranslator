#pragma once

#include "domain/model/ComicProject.h"
#include "domain/validation/ValidationResult.h"

#include <QByteArray>
#include <QJsonObject>

#include <expected>
#include <string>
#include <string_view>

namespace manga::data {

/// Serializer for converting between ComicProject domain aggregates and JSON format.
class ProjectSerializer {
public:
    /// Serializes a ComicProject into a formatted UTF-8 JSON byte array.
    [[nodiscard]] static QByteArray serialize(const domain::ComicProject& project);

    /// Serializes a ComicProject into a QJsonObject.
    [[nodiscard]] static QJsonObject serializeToJson(const domain::ComicProject& project);

    /// Deserializes a ComicProject from a JSON UTF-8 byte array with full structural and semantic validation.
    [[nodiscard]] static std::expected<domain::ComicProject, domain::ValidationError>
    deserialize(const QByteArray& jsonData);

    /// Deserializes a ComicProject from a string view.
    [[nodiscard]] static std::expected<domain::ComicProject, domain::ValidationError>
    deserialize(std::string_view jsonStr);

    /// Deserializes a ComicProject from a QJsonObject with validation and migration.
    [[nodiscard]] static std::expected<domain::ComicProject, domain::ValidationError>
    deserializeFromJson(const QJsonObject& rootObj);
};

} // namespace manga::data
