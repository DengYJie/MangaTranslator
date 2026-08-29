#include "data/project/ProjectSchemaValidator.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <unordered_set>

namespace manga::data {

namespace {

bool isStrictInteger(const QJsonValue& val) {
    if (!val.isDouble()) {
        return false;
    }
    const double d = val.toDouble();
    return std::isfinite(d) && std::floor(d) == d;
}

bool isNonNegativeInt(const QJsonValue& val) {
    if (!isStrictInteger(val)) {
        return false;
    }
    const double value = val.toDouble();
    return value >= 0.0 && value <= static_cast<double>(std::numeric_limits<int>::max());
}

bool isValidUuidV4String(const QString& str) {
    static const QRegularExpression regex(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-4[0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    return regex.match(str).hasMatch();
}

bool hasUnknownProperties(const QJsonObject& obj, const std::unordered_set<QString>& allowedKeys) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            return true;
        }
    }
    return false;
}

} // namespace

// Phase 1: Pre-migration envelope check

std::expected<void, domain::ValidationError> ProjectSchemaValidator::validateEnvelope(const QJsonObject& root) {
    // Only checks that the JSON is an object and contains valid version integers.
    // Does NOT validate content, required fields, or additionalProperties.
    if (!root.contains("schemaVersion") || !isStrictInteger(root["schemaVersion"]) ||
        root["schemaVersion"].toInt() < 1) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "schemaVersion must be an integer >= 1",
                                                       .path = "schemaVersion"});
    }
    if (!root.contains("formatVersion") || !isStrictInteger(root["formatVersion"]) ||
        root["formatVersion"].toInt() < 1) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "formatVersion must be an integer >= 1",
                                                       .path = "formatVersion"});
    }
    return {};
}

// Phase 2: Full structural validation (post-migration)

std::expected<void, domain::ValidationError> ProjectSchemaValidator::validateFull(const QJsonObject& root) {
    // Root allowed properties (isDirty is runtime-only, NOT persisted)
    static const std::unordered_set<QString> kRootAllowedKeys = {QStringLiteral("projectVersion"),
                                                                 QStringLiteral("schemaVersion"),
                                                                 QStringLiteral("formatVersion"),
                                                                 QStringLiteral("id"),
                                                                 QStringLiteral("name"),
                                                                 QStringLiteral("sourceLanguage"),
                                                                 QStringLiteral("targetLanguage"),
                                                                 QStringLiteral("readingDirection"),
                                                                 QStringLiteral("settings"),
                                                                 QStringLiteral("glossary"),
                                                                 QStringLiteral("pages")};

    if (hasUnknownProperties(root, kRootAllowedKeys)) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "Root object contains unknown properties (additionalProperties: false)",
                                    .path = ""});
    }

    // Required root fields
    static const QStringList kRootRequired = {QStringLiteral("projectVersion"),
                                              QStringLiteral("schemaVersion"),
                                              QStringLiteral("formatVersion"),
                                              QStringLiteral("id"),
                                              QStringLiteral("name"),
                                              QStringLiteral("sourceLanguage"),
                                              QStringLiteral("targetLanguage"),
                                              QStringLiteral("readingDirection"),
                                              QStringLiteral("pages")};

    for (const auto& req : kRootRequired) {
        if (!root.contains(req)) {
            return std::unexpected(
                domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                        .message = std::format("Missing required root field '{}'", req.toStdString()),
                                        .path = req.toStdString()});
        }
    }

    // projectVersion pattern
    if (!root["projectVersion"].isString()) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "projectVersion must be a string",
                                                       .path = "projectVersion"});
    }
    static const QRegularExpression kVersionRegex(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+.*$"));
    if (!kVersionRegex.match(root["projectVersion"].toString()).hasMatch()) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "projectVersion does not match semantic version pattern (e.g., '0.1.0')",
                                    .path = "projectVersion"});
    }

    // schemaVersion and formatVersion must be integers >= 1
    if (!isStrictInteger(root["schemaVersion"]) || root["schemaVersion"].toInt() < 1) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "schemaVersion must be an integer >= 1",
                                                       .path = "schemaVersion"});
    }
    if (!isStrictInteger(root["formatVersion"]) || root["formatVersion"].toInt() < 1) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "formatVersion must be an integer >= 1",
                                                       .path = "formatVersion"});
    }

    // id: strict UUID v4
    if (!root["id"].isString() || !isValidUuidV4String(root["id"].toString())) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                       .message = "id must be a valid RFC 4122 UUID v4 string",
                                                       .path = "id"});
    }

    // name
    if (!root["name"].isString() || root["name"].toString().isEmpty()) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "name must be a non-empty string",
                                                       .path = "name"});
    }

    // sourceLanguage, targetLanguage
    if (!root["sourceLanguage"].isString() || root["sourceLanguage"].toString().isEmpty()) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "sourceLanguage must be a non-empty string",
                                                       .path = "sourceLanguage"});
    }
    if (!root["targetLanguage"].isString() || root["targetLanguage"].toString().isEmpty()) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                       .message = "targetLanguage must be a non-empty string",
                                                       .path = "targetLanguage"});
    }

    // readingDirection enum
    const QString rd = root["readingDirection"].toString();
    if (rd != QStringLiteral("RightToLeft") && rd != QStringLiteral("LeftToRight")) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "readingDirection must be 'RightToLeft' or 'LeftToRight'",
                                    .path = "readingDirection"});
    }

    // settings (optional)
    if (root.contains("settings")) {
        if (!root["settings"].isObject()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "settings must be an object",
                                                           .path = "settings"});
        }
        static const std::unordered_set<QString> kSettingsAllowed = {QStringLiteral("defaultFontFamily"),
                                                                     QStringLiteral("defaultFontSizePt"),
                                                                     QStringLiteral("defaultOrientation"),
                                                                     QStringLiteral("readingDirection"),
                                                                     QStringLiteral("ocrEngine"),
                                                                     QStringLiteral("translatorEngine")};
        const QJsonObject sObj = root["settings"].toObject();
        if (hasUnknownProperties(sObj, kSettingsAllowed)) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "settings contains unknown properties",
                                                           .path = "settings"});
        }
        if (sObj.contains("defaultFontSizePt") &&
            (!sObj["defaultFontSizePt"].isDouble() || sObj["defaultFontSizePt"].toDouble() <= 0.0)) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "settings.defaultFontSizePt must be a number > 0",
                                                           .path = "settings.defaultFontSizePt"});
        }
        for (const auto& key :
             {QStringLiteral("defaultFontFamily"), QStringLiteral("ocrEngine"), QStringLiteral("translatorEngine")}) {
            if (sObj.contains(key) && !sObj[key].isString()) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = std::format("settings.{} must be a string", key.toStdString()),
                                            .path = "settings." + key.toStdString()});
            }
        }
        if (sObj.contains("defaultOrientation")) {
            const auto value = sObj["defaultOrientation"].toString();
            if (!sObj["defaultOrientation"].isString() ||
                (value != QStringLiteral("Horizontal") && value != QStringLiteral("Vertical"))) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "settings.defaultOrientation must be 'Horizontal' or 'Vertical'",
                                            .path = "settings.defaultOrientation"});
            }
        }
        if (sObj.contains("readingDirection")) {
            const auto value = sObj["readingDirection"].toString();
            if (!sObj["readingDirection"].isString() ||
                (value != QStringLiteral("RightToLeft") && value != QStringLiteral("LeftToRight"))) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "settings.readingDirection must be 'RightToLeft' or 'LeftToRight'",
                    .path = "settings.readingDirection"});
            }
        }
    }

    // glossary (optional)
    if (root.contains("glossary")) {
        if (!root["glossary"].isArray()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "glossary must be an array",
                                                           .path = "glossary"});
        }
        const QJsonArray gArr = root["glossary"].toArray();
        static const std::unordered_set<QString> kGlossaryAllowed = {QStringLiteral("id"),
                                                                     QStringLiteral("source"),
                                                                     QStringLiteral("target"),
                                                                     QStringLiteral("category"),
                                                                     QStringLiteral("caseSensitive"),
                                                                     QStringLiteral("enabled")};
        for (size_t gIdx = 0; gIdx < static_cast<size_t>(gArr.size()); ++gIdx) {
            const std::string gPath = std::format("glossary[{}]", gIdx);
            if (!gArr[static_cast<qsizetype>(gIdx)].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary item must be an object",
                                                               .path = gPath});
            }
            const QJsonObject gObj = gArr[static_cast<qsizetype>(gIdx)].toObject();
            if (hasUnknownProperties(gObj, kGlossaryAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary item contains unknown properties",
                                                               .path = gPath});
            }
            if (!gObj.contains("id") || !gObj["id"].isString() || !isValidUuidV4String(gObj["id"].toString())) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                            .message = "glossary item id must be a valid UUID v4 string",
                                            .path = gPath + ".id"});
            }
            if (!gObj.contains("source") || !gObj["source"].isString() || gObj["source"].toString().isEmpty()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary source must be a non-empty string",
                                                               .path = gPath + ".source"});
            }
            if (!gObj.contains("target") || !gObj["target"].isString() || gObj["target"].toString().isEmpty()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary target must be a non-empty string",
                                                               .path = gPath + ".target"});
            }
            if (gObj.contains("caseSensitive") && !gObj["caseSensitive"].isBool()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary caseSensitive must be a boolean",
                                                               .path = gPath + ".caseSensitive"});
            }
            if (gObj.contains("category") && !gObj["category"].isString()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary category must be a string",
                                                               .path = gPath + ".category"});
            }
            if (gObj.contains("enabled") && !gObj["enabled"].isBool()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "glossary enabled must be a boolean",
                                                               .path = gPath + ".enabled"});
            }
        }
    }

    // pages
    if (!root["pages"].isArray()) {
        return std::unexpected(domain::ValidationError{
            .code = domain::ProjectErrorCode::InvalidStructure, .message = "pages must be an array", .path = "pages"});
    }

    const QJsonArray pagesArr = root["pages"].toArray();
    static const std::unordered_set<QString> kPageAllowed = {QStringLiteral("id"),
                                                             QStringLiteral("originalAsset"),
                                                             QStringLiteral("imageSize"),
                                                             QStringLiteral("orderIndex"),
                                                             QStringLiteral("layers"),
                                                             QStringLiteral("regions")};
    static const std::unordered_set<QString> kImageSizeAllowed = {QStringLiteral("width"), QStringLiteral("height")};

    for (size_t pIdx = 0; pIdx < static_cast<size_t>(pagesArr.size()); ++pIdx) {
        const std::string pagePath = std::format("pages[{}]", pIdx);
        if (!pagesArr[static_cast<qsizetype>(pIdx)].isObject()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page must be an object",
                                                           .path = pagePath});
        }
        const QJsonObject pObj = pagesArr[static_cast<qsizetype>(pIdx)].toObject();
        if (hasUnknownProperties(pObj, kPageAllowed)) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page contains unknown properties",
                                                           .path = pagePath});
        }

        static const QStringList kPageRequired = {QStringLiteral("id"),
                                                  QStringLiteral("originalAsset"),
                                                  QStringLiteral("imageSize"),
                                                  QStringLiteral("orderIndex"),
                                                  QStringLiteral("regions")};
        for (const auto& req : kPageRequired) {
            if (!pObj.contains(req)) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = std::format("Missing required page property '{}'", req.toStdString()),
                    .path = pagePath + "." + req.toStdString()});
            }
        }

        if (!pObj["id"].isString() || !isValidUuidV4String(pObj["id"].toString())) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                           .message = "page id must be a valid UUID v4 string",
                                                           .path = pagePath + ".id"});
        }
        if (!pObj["originalAsset"].isString() || pObj["originalAsset"].toString().isEmpty()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page originalAsset must be a non-empty string",
                                                           .path = pagePath + ".originalAsset"});
        }
        if (!isNonNegativeInt(pObj["orderIndex"])) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page orderIndex must be an integer >= 0",
                                                           .path = pagePath + ".orderIndex"});
        }
        // Strictly validate orderIndex matches array position
        if (pObj["orderIndex"].toInt() != static_cast<int>(pIdx)) {
            return std::unexpected(domain::ValidationError{
                .code = domain::ProjectErrorCode::DuplicateOrderIndex,
                .message = std::format(
                    "page orderIndex must be {} (array position), got {}", pIdx, pObj["orderIndex"].toInt()),
                .path = pagePath + ".orderIndex"});
        }

        if (!pObj["imageSize"].isObject()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page imageSize must be an object",
                                                           .path = pagePath + ".imageSize"});
        }
        const QJsonObject sizeObj = pObj["imageSize"].toObject();
        if (hasUnknownProperties(sizeObj, kImageSizeAllowed)) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "imageSize contains unknown properties",
                                                           .path = pagePath + ".imageSize"});
        }
        if (!sizeObj.contains("width") || !sizeObj.contains("height") || !isStrictInteger(sizeObj["width"]) ||
            !isStrictInteger(sizeObj["height"]) || sizeObj["width"].toInt() < 1 || sizeObj["height"].toInt() < 1) {
            return std::unexpected(
                domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                        .message = "imageSize width and height must be integers >= 1",
                                        .path = pagePath + ".imageSize"});
        }

        if (!pObj["regions"].isArray()) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                           .message = "page regions must be an array",
                                                           .path = pagePath + ".regions"});
        }

        if (pObj.contains("layers")) {
            if (!pObj["layers"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "page layers must be an object",
                                                               .path = pagePath + ".layers"});
            }
            const QJsonObject layersObj = pObj["layers"].toObject();
            static const std::unordered_set<QString> kLayersAllowed = {
                QStringLiteral("clear"), QStringLiteral("translation"), QStringLiteral("manualOverride")};
            if (hasUnknownProperties(layersObj, kLayersAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "page layers contains unknown properties",
                                                               .path = pagePath + ".layers"});
            }
            for (auto it = layersObj.begin(); it != layersObj.end(); ++it) {
                if (!it.value().isString()) {
                    return std::unexpected(
                        domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                .message = "page layer references must be strings",
                                                .path = pagePath + ".layers." + it.key().toStdString()});
                }
            }
        }

        const QJsonArray rArr = pObj["regions"].toArray();
        static const std::unordered_set<QString> kRegionAllowed = {QStringLiteral("id"),
                                                                   QStringLiteral("orderIndex"),
                                                                   QStringLiteral("geometry"),
                                                                   QStringLiteral("source"),
                                                                   QStringLiteral("translation"),
                                                                   QStringLiteral("layout"),
                                                                   QStringLiteral("clear"),
                                                                   QStringLiteral("enabled"),
                                                                   QStringLiteral("locked"),
                                                                   QStringLiteral("translationVisible"),
                                                                   QStringLiteral("isCleared"),
                                                                   QStringLiteral("contentRevision")};
        static const std::unordered_set<QString> kGeoAllowed = {QStringLiteral("type"),
                                                                QStringLiteral("x"),
                                                                QStringLiteral("y"),
                                                                QStringLiteral("width"),
                                                                QStringLiteral("height")};
        static const std::unordered_set<QString> kSrcAllowed = {QStringLiteral("text"),
                                                                QStringLiteral("sourceType"),
                                                                QStringLiteral("confidence"),
                                                                QStringLiteral("ocrEngine"),
                                                                QStringLiteral("revision")};
        static const std::unordered_set<QString> kTransAllowed = {QStringLiteral("modelText"),
                                                                  QStringLiteral("selectedCandidateId"),
                                                                  QStringLiteral("manualText"),
                                                                  QStringLiteral("candidates")};
        static const std::unordered_set<QString> kCandAllowed = {
            QStringLiteral("id"), QStringLiteral("text"), QStringLiteral("confidence"), QStringLiteral("provider")};
        static const std::unordered_set<QString> kLayoutAllowed = {QStringLiteral("fontFamily"),
                                                                   QStringLiteral("fontSizePt"),
                                                                   QStringLiteral("fontWeight"),
                                                                   QStringLiteral("letterSpacingEm"),
                                                                   QStringLiteral("textColorHex"),
                                                                   QStringLiteral("strokeColorHex"),
                                                                   QStringLiteral("strokeWidthPx"),
                                                                   QStringLiteral("orientation"),
                                                                   QStringLiteral("alignment"),
                                                                   QStringLiteral("lineSpacing"),
                                                                   QStringLiteral("margins"),
                                                                   QStringLiteral("wordWrap"),
                                                                   QStringLiteral("autoFit"),
                                                                   QStringLiteral("minFontSizePt"),
                                                                   QStringLiteral("maxFontSizePt"),
                                                                   QStringLiteral("rotationDeg")};
        static const std::unordered_set<QString> kAlignAllowed = {QStringLiteral("horizontal"),
                                                                  QStringLiteral("vertical")};
        static const std::unordered_set<QString> kMarginsAllowed = {
            QStringLiteral("top"), QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left")};
        static const std::unordered_set<QString> kClearAllowed = {
            QStringLiteral("enabled"), QStringLiteral("colorHex"), QStringLiteral("opacity")};

        for (size_t rIdx = 0; rIdx < static_cast<size_t>(rArr.size()); ++rIdx) {
            const std::string regionPath = std::format("{}.regions[{}]", pagePath, rIdx);
            if (!rArr[static_cast<qsizetype>(rIdx)].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region must be an object",
                                                               .path = regionPath});
            }
            const QJsonObject rObj = rArr[static_cast<qsizetype>(rIdx)].toObject();
            if (hasUnknownProperties(rObj, kRegionAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region contains unknown properties",
                                                               .path = regionPath});
            }

            static const QStringList kRegionRequired = {QStringLiteral("id"),
                                                        QStringLiteral("orderIndex"),
                                                        QStringLiteral("geometry"),
                                                        QStringLiteral("source"),
                                                        QStringLiteral("translation"),
                                                        QStringLiteral("layout"),
                                                        QStringLiteral("clear")};
            for (const auto& req : kRegionRequired) {
                if (!rObj.contains(req)) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("Missing required region property '{}'", req.toStdString()),
                        .path = regionPath + "." + req.toStdString()});
                }
            }

            if (!rObj["id"].isString() || !isValidUuidV4String(rObj["id"].toString())) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                               .message = "region id must be a valid UUID v4 string",
                                                               .path = regionPath + ".id"});
            }
            if (!isNonNegativeInt(rObj["orderIndex"])) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region orderIndex must be an integer >= 0",
                                                               .path = regionPath + ".orderIndex"});
            }
            // Strictly validate region orderIndex matches array position
            if (rObj["orderIndex"].toInt() != static_cast<int>(rIdx)) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::DuplicateOrderIndex,
                    .message = std::format(
                        "region orderIndex must be {} (array position), got {}", rIdx, rObj["orderIndex"].toInt()),
                    .path = regionPath + ".orderIndex"});
            }

            // Geometry
            if (!rObj["geometry"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region geometry must be an object",
                                                               .path = regionPath + ".geometry"});
            }
            const QJsonObject geoObj = rObj["geometry"].toObject();
            if (hasUnknownProperties(geoObj, kGeoAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "geometry contains unknown properties",
                                                               .path = regionPath + ".geometry"});
            }
            if (!geoObj.contains("type") || geoObj["type"].toString() != QStringLiteral("rect")) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "geometry type must be 'rect'",
                                                               .path = regionPath + ".geometry.type"});
            }
            for (const auto& coord :
                 {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")}) {
                if (!geoObj.contains(coord) || !geoObj[coord].isDouble()) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("geometry.{} must be a number", coord.toStdString()),
                        .path = regionPath + ".geometry." + coord.toStdString()});
                }
            }
            if (geoObj["x"].toDouble() < 0.0 || geoObj["y"].toDouble() < 0.0 || geoObj["width"].toDouble() < 2.0 ||
                geoObj["height"].toDouble() < 2.0) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "geometry requires x/y >= 0 and width/height >= 2",
                                            .path = regionPath + ".geometry"});
            }

            // Source
            if (!rObj["source"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region source must be an object",
                                                               .path = regionPath + ".source"});
            }
            const QJsonObject srcObj = rObj["source"].toObject();
            if (hasUnknownProperties(srcObj, kSrcAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "source contains unknown properties",
                                                               .path = regionPath + ".source"});
            }
            if (!srcObj.contains("text") || !srcObj["text"].isString() || !srcObj.contains("sourceType") ||
                !srcObj["sourceType"].isString()) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "source must contain text and sourceType strings",
                                            .path = regionPath + ".source"});
            }
            const QString st = srcObj["sourceType"].toString();
            if (st != QStringLiteral("OcrAuto") && st != QStringLiteral("OcrManualModified") &&
                st != QStringLiteral("PureManual")) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "sourceType must be 'OcrAuto', 'OcrManualModified', or 'PureManual'",
                    .path = regionPath + ".source.sourceType"});
            }
            if (srcObj.contains("confidence")) {
                if (!srcObj["confidence"].isDouble() || srcObj["confidence"].toDouble() < 0.0 ||
                    srcObj["confidence"].toDouble() > 1.0) {
                    return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                                   .message = "source.confidence must be in [0.0, 1.0]",
                                                                   .path = regionPath + ".source.confidence"});
                }
            }
            if (srcObj.contains("ocrEngine") && !srcObj["ocrEngine"].isString()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "source.ocrEngine must be a string",
                                                               .path = regionPath + ".source.ocrEngine"});
            }
            if (srcObj.contains("revision") &&
                (!isStrictInteger(srcObj["revision"]) || srcObj["revision"].toDouble() < 0.0)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "source.revision must be an integer >= 0",
                                                               .path = regionPath + ".source.revision"});
            }

            // Translation
            if (!rObj["translation"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region translation must be an object",
                                                               .path = regionPath + ".translation"});
            }
            const QJsonObject transObj = rObj["translation"].toObject();
            if (hasUnknownProperties(transObj, kTransAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "translation contains unknown properties",
                                                               .path = regionPath + ".translation"});
            }
            for (const auto& key :
                 {QStringLiteral("modelText"), QStringLiteral("selectedCandidateId"), QStringLiteral("manualText")}) {
                if (transObj.contains(key) && !transObj[key].isString() && !transObj[key].isNull()) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("translation.{} must be a string or null", key.toStdString()),
                        .path = regionPath + ".translation." + key.toStdString()});
                }
            }
            if (transObj.contains("candidates") && !transObj["candidates"].isArray()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "translation.candidates must be an array",
                                                               .path = regionPath + ".translation.candidates"});
            }
            if (transObj.contains("candidates")) {
                const QJsonArray candArr = transObj["candidates"].toArray();
                for (size_t cIdx = 0; cIdx < static_cast<size_t>(candArr.size()); ++cIdx) {
                    const std::string cPath = std::format("{}.translation.candidates[{}]", regionPath, cIdx);
                    if (!candArr[static_cast<qsizetype>(cIdx)].isObject()) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate must be an object",
                                                    .path = cPath});
                    }
                    const QJsonObject cObj = candArr[static_cast<qsizetype>(cIdx)].toObject();
                    if (hasUnknownProperties(cObj, kCandAllowed)) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate contains unknown properties",
                                                    .path = cPath});
                    }
                    if (!cObj.contains("id") || !cObj["id"].isString() || cObj["id"].toString().isEmpty()) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate must have a non-empty string id",
                                                    .path = cPath + ".id"});
                    }
                    if (!cObj.contains("text") || !cObj["text"].isString()) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate must have a string text",
                                                    .path = cPath + ".text"});
                    }
                    if (cObj.contains("confidence") &&
                        (!cObj["confidence"].isDouble() || cObj["confidence"].toDouble() < 0.0 ||
                         cObj["confidence"].toDouble() > 1.0)) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate.confidence must be in [0.0, 1.0]",
                                                    .path = cPath + ".confidence"});
                    }
                    if (cObj.contains("provider") && !cObj["provider"].isString()) {
                        return std::unexpected(
                            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                    .message = "candidate.provider must be a string",
                                                    .path = cPath + ".provider"});
                    }
                }
            }

            // Layout
            if (!rObj["layout"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region layout must be an object",
                                                               .path = regionPath + ".layout"});
            }
            const QJsonObject layObj = rObj["layout"].toObject();
            if (hasUnknownProperties(layObj, kLayoutAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout contains unknown properties",
                                                               .path = regionPath + ".layout"});
            }
            if (!layObj.contains("fontFamily") || !layObj["fontFamily"].isString() || !layObj.contains("fontSizePt") ||
                !layObj["fontSizePt"].isDouble() || layObj["fontSizePt"].toDouble() <= 0.0 ||
                !layObj.contains("orientation") || !layObj["orientation"].isString() ||
                !layObj.contains("lineSpacing") || !layObj["lineSpacing"].isDouble() ||
                layObj["lineSpacing"].toDouble() <= 0.0) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message =
                        "layout must contain valid fontFamily, fontSizePt (>0), orientation, and lineSpacing (>0)",
                    .path = regionPath + ".layout"});
            }

            if (!layObj.contains("fontWeight") || !isStrictInteger(layObj["fontWeight"])) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout must contain fontWeight integer",
                                                               .path = regionPath + ".layout.fontWeight"});
            }
            int fw = layObj["fontWeight"].toInt();
            if (fw < 100 || fw > 900 || (fw % 100) != 0) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "fontWeight must be between 100 and 900 in multiples of 100",
                                            .path = regionPath + ".layout.fontWeight"});
            }

            if (!layObj.contains("letterSpacingEm") || !layObj["letterSpacingEm"].isDouble()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout must contain letterSpacingEm number",
                                                               .path = regionPath + ".layout.letterSpacingEm"});
            }
            double ls = layObj["letterSpacingEm"].toDouble();
            if (ls < -0.5 || ls > 2.0) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "letterSpacingEm must be between -0.5 and 2.0",
                                            .path = regionPath + ".layout.letterSpacingEm"});
            }

            static const QRegularExpression hexColorRegex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
            if (!layObj.contains("textColorHex") || !layObj["textColorHex"].isString() ||
                !hexColorRegex.match(layObj["textColorHex"].toString()).hasMatch()) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "textColorHex must be a valid hex color string",
                                            .path = regionPath + ".layout.textColorHex"});
            }
            if (!layObj.contains("strokeColorHex") || !layObj["strokeColorHex"].isString() ||
                !hexColorRegex.match(layObj["strokeColorHex"].toString()).hasMatch()) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "strokeColorHex must be a valid hex color string",
                                            .path = regionPath + ".layout.strokeColorHex"});
            }

            if (!layObj.contains("strokeWidthPx") || !layObj["strokeWidthPx"].isDouble()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout must contain strokeWidthPx number",
                                                               .path = regionPath + ".layout.strokeWidthPx"});
            }
            double sw = layObj["strokeWidthPx"].toDouble();
            if (sw < 0.0 || sw > 32.0) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "strokeWidthPx must be between 0 and 32",
                                                               .path = regionPath + ".layout.strokeWidthPx"});
            }
            const QString orientation = layObj["orientation"].toString();
            if (orientation != QStringLiteral("Horizontal") && orientation != QStringLiteral("Vertical")) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "layout.orientation must be 'Horizontal' or 'Vertical'",
                                            .path = regionPath + ".layout.orientation"});
            }
            // alignment (required)
            if (!layObj.contains("alignment") || !layObj["alignment"].isObject()) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "layout.alignment must be an object with horizontal and vertical fields",
                    .path = regionPath + ".layout.alignment"});
            }
            const QJsonObject alignObj = layObj["alignment"].toObject();
            if (hasUnknownProperties(alignObj, kAlignAllowed)) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "layout.alignment contains unknown properties",
                                            .path = regionPath + ".layout.alignment"});
            }
            if (!alignObj.contains("horizontal") || !alignObj["horizontal"].isString() ||
                !alignObj.contains("vertical") || !alignObj["vertical"].isString()) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "layout.alignment must have horizontal and vertical string fields",
                    .path = regionPath + ".layout.alignment"});
            }
            const QString horizontal = alignObj["horizontal"].toString();
            const QString vertical = alignObj["vertical"].toString();
            if (horizontal != QStringLiteral("Left") && horizontal != QStringLiteral("Center") &&
                horizontal != QStringLiteral("Right") && horizontal != QStringLiteral("Justify")) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "layout.alignment.horizontal has an invalid value",
                                            .path = regionPath + ".layout.alignment.horizontal"});
            }
            if (vertical != QStringLiteral("Top") && vertical != QStringLiteral("Middle") &&
                vertical != QStringLiteral("Bottom")) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "layout.alignment.vertical has an invalid value",
                                            .path = regionPath + ".layout.alignment.vertical"});
            }
            // margins (required)
            if (!layObj.contains("margins") || !layObj["margins"].isObject()) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "layout.margins must be an object with top/right/bottom/left fields",
                    .path = regionPath + ".layout.margins"});
            }
            const QJsonObject margObj = layObj["margins"].toObject();
            if (hasUnknownProperties(margObj, kMarginsAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout.margins contains unknown properties",
                                                               .path = regionPath + ".layout.margins"});
            }
            for (const auto& side :
                 {QStringLiteral("top"), QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left")}) {
                if (!margObj.contains(side) || !margObj[side].isDouble() || margObj[side].toDouble() < 0.0) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("layout.margins.{} must be a number >= 0", side.toStdString()),
                        .path = regionPath + ".layout.margins." + side.toStdString()});
                }
            }
            for (const auto& key : {QStringLiteral("wordWrap"), QStringLiteral("autoFit")}) {
                if (layObj.contains(key) && !layObj[key].isBool()) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("layout.{} must be a boolean", key.toStdString()),
                        .path = regionPath + ".layout." + key.toStdString()});
                }
            }
            for (const auto& key : {QStringLiteral("minFontSizePt"), QStringLiteral("maxFontSizePt")}) {
                if (layObj.contains(key) && (!layObj[key].isDouble() || layObj[key].toDouble() <= 0.0)) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("layout.{} must be a number > 0", key.toStdString()),
                        .path = regionPath + ".layout." + key.toStdString()});
                }
            }
            if (layObj.contains("rotationDeg") && !layObj["rotationDeg"].isDouble()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout.rotationDeg must be a number",
                                                               .path = regionPath + ".layout.rotationDeg"});
            }
            if (layObj.contains("fontWeight") &&
                (!layObj["fontWeight"].isDouble() || layObj["fontWeight"].toDouble() < 100.0 ||
                 layObj["fontWeight"].toDouble() > 900.0)) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "layout.fontWeight must be between 100 and 900",
                                            .path = regionPath + ".layout.fontWeight"});
            }
            if (layObj.contains("letterSpacingEm") && !layObj["letterSpacingEm"].isDouble()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout.letterSpacingEm must be a number",
                                                               .path = regionPath + ".layout.letterSpacingEm"});
            }
            for (const auto& key : {QStringLiteral("textColorHex"), QStringLiteral("strokeColorHex")}) {
                if (layObj.contains(key) && !layObj[key].isString()) {
                    return std::unexpected(
                        domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                .message = std::format("layout.{} must be a string", key.toStdString()),
                                                .path = regionPath + ".layout." + key.toStdString()});
                }
            }
            if (layObj.contains("strokeWidthPx") &&
                (!layObj["strokeWidthPx"].isDouble() || layObj["strokeWidthPx"].toDouble() < 0.0)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "layout.strokeWidthPx must be a number >= 0",
                                                               .path = regionPath + ".layout.strokeWidthPx"});
            }

            // Clear
            if (!rObj["clear"].isObject()) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "region clear must be an object",
                                                               .path = regionPath + ".clear"});
            }
            const QJsonObject clrObj = rObj["clear"].toObject();
            if (hasUnknownProperties(clrObj, kClearAllowed)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                                               .message = "clear contains unknown properties",
                                                               .path = regionPath + ".clear"});
            }
            if (!clrObj.contains("enabled") || !clrObj["enabled"].isBool() || !clrObj.contains("colorHex") ||
                !clrObj["colorHex"].isString() || !clrObj.contains("opacity") || !clrObj["opacity"].isDouble() ||
                clrObj["opacity"].toDouble() < 0.0 || clrObj["opacity"].toDouble() > 1.0) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidStructure,
                    .message = "clear must contain enabled (bool), colorHex (string), and opacity in [0.0, 1.0]",
                    .path = regionPath + ".clear"});
            }

            for (const auto& flag : {QStringLiteral("enabled"),
                                     QStringLiteral("locked"),
                                     QStringLiteral("translationVisible"),
                                     QStringLiteral("isCleared")}) {
                if (rObj.contains(flag) && !rObj[flag].isBool()) {
                    return std::unexpected(domain::ValidationError{
                        .code = domain::ProjectErrorCode::InvalidStructure,
                        .message = std::format("region.{} must be a boolean", flag.toStdString()),
                        .path = regionPath + "." + flag.toStdString()});
                }
            }
            if (rObj.contains("contentRevision") &&
                (!isStrictInteger(rObj["contentRevision"]) || rObj["contentRevision"].toDouble() < 0.0)) {
                return std::unexpected(
                    domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                            .message = "region.contentRevision must be an integer >= 0",
                                            .path = regionPath + ".contentRevision"});
            }
        }
    }

    return {};
}

} // namespace manga::data
