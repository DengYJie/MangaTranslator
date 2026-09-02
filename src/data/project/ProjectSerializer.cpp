#include "data/project/ProjectSerializer.h"

#include "data/project/ProjectMigrator.h"
#include "data/project/ProjectSchemaValidator.h"
#include "domain/model/RehydrationBuilder.h"
#include "domain/validation/SemanticValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <format>

namespace manga::data {

namespace {

// The JSON keys and enum literals in this file are the on-disk format of project.json.
// serializeToJson writes them, the stringTo helpers read them back, and
// ProjectSchemaValidator::validateFull accepts the same key set and the same enum
// spellings, with no alias and no case-insensitive fallback. A rename on one side only
// makes the next save produce a document that no longer loads, while files already
// written keep the old spelling, so the corpus splits in two.

std::string orientationToString(domain::TextOrientation orientation) {
    return orientation == domain::TextOrientation::Vertical ? "Vertical" : "Horizontal";
}

std::optional<domain::TextOrientation> stringToOrientation(const QString& str) {
    if (str == "Vertical") {
        return domain::TextOrientation::Vertical;
    }
    if (str == "Horizontal") {
        return domain::TextOrientation::Horizontal;
    }
    return std::nullopt;
}

std::string readingDirectionToString(domain::ReadingDirection dir) {
    return dir == domain::ReadingDirection::LeftToRight ? "LeftToRight" : "RightToLeft";
}

std::optional<domain::ReadingDirection> stringToReadingDirection(const QString& str) {
    if (str == "LeftToRight") {
        return domain::ReadingDirection::LeftToRight;
    }
    if (str == "RightToLeft") {
        return domain::ReadingDirection::RightToLeft;
    }
    return std::nullopt;
}

std::string horizontalAlignToString(domain::HorizontalAlignment align) {
    switch (align) {
        case domain::HorizontalAlignment::Left:
            return "Left";
        case domain::HorizontalAlignment::Center:
            return "Center";
        case domain::HorizontalAlignment::Right:
            return "Right";
        case domain::HorizontalAlignment::Justify:
            return "Justify";
    }
    return "Center";
}

std::optional<domain::HorizontalAlignment> stringToHorizontalAlign(const QString& str) {
    if (str == "Left")
        return domain::HorizontalAlignment::Left;
    if (str == "Center")
        return domain::HorizontalAlignment::Center;
    if (str == "Right")
        return domain::HorizontalAlignment::Right;
    if (str == "Justify")
        return domain::HorizontalAlignment::Justify;
    return std::nullopt;
}

std::string verticalAlignToString(domain::VerticalAlignment align) {
    switch (align) {
        case domain::VerticalAlignment::Top:
            return "Top";
        case domain::VerticalAlignment::Middle:
            return "Middle";
        case domain::VerticalAlignment::Bottom:
            return "Bottom";
    }
    return "Middle";
}

std::optional<domain::VerticalAlignment> stringToVerticalAlign(const QString& str) {
    if (str == "Top")
        return domain::VerticalAlignment::Top;
    if (str == "Middle")
        return domain::VerticalAlignment::Middle;
    if (str == "Bottom")
        return domain::VerticalAlignment::Bottom;
    return std::nullopt;
}

std::string sourceTypeToString(domain::TextSourceType type) {
    switch (type) {
        case domain::TextSourceType::OcrAuto:
            return "OcrAuto";
        case domain::TextSourceType::OcrManualModified:
            return "OcrManualModified";
        case domain::TextSourceType::PureManual:
            return "PureManual";
    }
    return "PureManual";
}

std::optional<domain::TextSourceType> stringToSourceType(const QString& str) {
    if (str == "OcrAuto")
        return domain::TextSourceType::OcrAuto;
    if (str == "OcrManualModified")
        return domain::TextSourceType::OcrManualModified;
    if (str == "PureManual")
        return domain::TextSourceType::PureManual;
    return std::nullopt;
}

} // namespace

QJsonObject ProjectSerializer::serializeToJson(const domain::ComicProject& project) {
    QJsonObject root;
    root[QStringLiteral("projectVersion")] = QString::fromStdString(project.projectVersion());
    root[QStringLiteral("schemaVersion")] = project.schemaVersion();
    root[QStringLiteral("formatVersion")] = project.formatVersion();
    root[QStringLiteral("id")] = QString::fromStdString(project.id().toString());
    root[QStringLiteral("name")] = QString::fromStdString(project.name());
    // isDirty is runtime-only state, so this writer never emits a key for it.
    root[QStringLiteral("sourceLanguage")] = QString::fromStdString(project.sourceLanguage().toString());
    root[QStringLiteral("targetLanguage")] = QString::fromStdString(project.targetLanguage().toString());
    root[QStringLiteral("readingDirection")] =
        QString::fromStdString(readingDirectionToString(project.readingDirection()));

    // Settings
    QJsonObject settingsObj;
    settingsObj["defaultFontFamily"] = QString::fromStdString(project.settings().defaultFontFamily);
    settingsObj["defaultFontSizePt"] = project.settings().defaultFontSizePt;
    settingsObj["defaultOrientation"] =
        QString::fromStdString(orientationToString(project.settings().defaultOrientation));
    settingsObj["readingDirection"] =
        QString::fromStdString(readingDirectionToString(project.settings().readingDirection));
    settingsObj["ocrEngine"] = QString::fromStdString(project.settings().ocrEngine);
    settingsObj["translatorEngine"] = QString::fromStdString(project.settings().translatorEngine);
    root["settings"] = settingsObj;

    // Glossary
    QJsonArray glossaryArr;
    for (const auto& g : project.glossary()) {
        QJsonObject gObj;
        gObj["id"] = QString::fromStdString(g.id.toString());
        gObj["source"] = QString::fromStdString(g.source);
        gObj["target"] = QString::fromStdString(g.target);
        if (!g.category.empty()) {
            gObj["category"] = QString::fromStdString(g.category);
        }
        gObj["caseSensitive"] = g.caseSensitive;
        gObj["enabled"] = g.enabled;
        glossaryArr.append(gObj);
    }
    root["glossary"] = glossaryArr;

    // Pages
    QJsonArray pagesArr;
    for (const auto& page : project.pages()) {
        QJsonObject pageObj;
        pageObj["id"] = QString::fromStdString(page.id().toString());
        pageObj["originalAsset"] = QString::fromStdString(page.originalAssetPath());

        QJsonObject sizeObj;
        sizeObj["width"] = page.imageSize().width;
        sizeObj["height"] = page.imageSize().height;
        pageObj["imageSize"] = sizeObj;

        pageObj["orderIndex"] = page.orderIndex();

        // Layers
        if (page.clearLayerAsset().has_value() || page.translationLayerAsset().has_value() ||
            page.manualOverrideLayerAsset().has_value()) {
            QJsonObject layersObj;
            if (page.clearLayerAsset().has_value()) {
                layersObj["clear"] = QString::fromStdString(*page.clearLayerAsset());
            }
            if (page.translationLayerAsset().has_value()) {
                layersObj["translation"] = QString::fromStdString(*page.translationLayerAsset());
            }
            if (page.manualOverrideLayerAsset().has_value()) {
                layersObj["manualOverride"] = QString::fromStdString(*page.manualOverrideLayerAsset());
            }
            pageObj["layers"] = layersObj;
        }

        // Regions
        QJsonArray regionsArr;
        for (const auto& region : page.regions()) {
            QJsonObject regionObj;
            regionObj["id"] = QString::fromStdString(region.id().toString());
            regionObj["orderIndex"] = region.orderIndex();

            // Geometry
            QJsonObject geoObj;
            geoObj["type"] = "rect";
            geoObj["x"] = region.geometry().x;
            geoObj["y"] = region.geometry().y;
            geoObj["width"] = region.geometry().width;
            geoObj["height"] = region.geometry().height;
            regionObj["geometry"] = geoObj;

            if (region.bubbleGeometry().has_value()) {
                QJsonObject bGeoObj;
                bGeoObj["x"] = region.bubbleGeometry()->x;
                bGeoObj["y"] = region.bubbleGeometry()->y;
                bGeoObj["width"] = region.bubbleGeometry()->width;
                bGeoObj["height"] = region.bubbleGeometry()->height;
                regionObj["bubbleGeometry"] = bGeoObj;
            }
            if (region.bubblePolygon().has_value() && region.bubblePolygon()->isValid()) {
                QJsonArray polyArr;
                for (const auto& pt : region.bubblePolygon()->points) {
                    QJsonObject ptObj;
                    ptObj["x"] = pt.x;
                    ptObj["y"] = pt.y;
                    polyArr.append(ptObj);
                }
                regionObj["bubblePolygon"] = polyArr;
            }

            // Source
            QJsonObject srcObj;
            srcObj["text"] = QString::fromStdString(region.source().text);
            srcObj["sourceType"] = QString::fromStdString(sourceTypeToString(region.source().sourceType));
            srcObj["confidence"] = region.source().confidence;
            srcObj["ocrEngine"] = QString::fromStdString(region.source().ocrEngine);
            srcObj["revision"] = static_cast<qint64>(region.source().revision);
            regionObj["source"] = srcObj;

            // Translation
            QJsonObject transObj;
            if (region.translation().modelText().has_value()) {
                transObj["modelText"] = QString::fromStdString(*region.translation().modelText());
            } else {
                transObj["modelText"] = QJsonValue::Null;
            }
            if (region.translation().selectedCandidateId().has_value()) {
                transObj["selectedCandidateId"] = QString::fromStdString(*region.translation().selectedCandidateId());
            } else {
                transObj["selectedCandidateId"] = QJsonValue::Null;
            }
            if (region.translation().manualText().has_value()) {
                transObj["manualText"] = QString::fromStdString(*region.translation().manualText());
            } else {
                transObj["manualText"] = QJsonValue::Null;
            }

            QJsonArray candArr;
            for (const auto& c : region.translation().candidates()) {
                QJsonObject cObj;
                cObj["id"] = QString::fromStdString(c.id);
                cObj["text"] = QString::fromStdString(c.text);
                cObj["confidence"] = c.confidence;
                cObj["provider"] = QString::fromStdString(c.provider);
                candArr.append(cObj);
            }
            transObj["candidates"] = candArr;
            regionObj["translation"] = transObj;

            // Layout
            QJsonObject layoutObj;
            layoutObj["fontFamily"] = QString::fromStdString(region.layout().fontFamily);
            layoutObj["fontSizePt"] = region.layout().fontSizePt;
            layoutObj["fontWeight"] = region.layout().fontWeight;
            layoutObj["letterSpacingEm"] = region.layout().letterSpacingEm;
            layoutObj["textColorHex"] = QString::fromStdString(region.layout().textColorHex);
            layoutObj["strokeColorHex"] = QString::fromStdString(region.layout().strokeColorHex);
            layoutObj["strokeWidthPx"] = region.layout().strokeWidthPx;
            layoutObj["orientation"] = QString::fromStdString(orientationToString(region.layout().orientation));

            QJsonObject alignObj;
            alignObj["horizontal"] =
                QString::fromStdString(horizontalAlignToString(region.layout().alignment.horizontal));
            alignObj["vertical"] = QString::fromStdString(verticalAlignToString(region.layout().alignment.vertical));
            layoutObj["alignment"] = alignObj;

            layoutObj["lineSpacing"] = region.layout().lineSpacing;

            QJsonObject marginsObj;
            marginsObj["top"] = region.layout().margins.top;
            marginsObj["right"] = region.layout().margins.right;
            marginsObj["bottom"] = region.layout().margins.bottom;
            marginsObj["left"] = region.layout().margins.left;
            layoutObj["margins"] = marginsObj;

            layoutObj["wordWrap"] = region.layout().wordWrap;
            layoutObj["autoFit"] = region.layout().autoFit;
            layoutObj["minFontSizePt"] = region.layout().minFontSizePt;
            layoutObj["maxFontSizePt"] = region.layout().maxFontSizePt;
            layoutObj["rotationDeg"] = region.layout().rotationDeg;
            regionObj["layout"] = layoutObj;

            // Clear
            QJsonObject clearObj;
            clearObj["enabled"] = region.clear().enabled;
            clearObj["colorHex"] = QString::fromStdString(region.clear().colorHex);
            clearObj["opacity"] = region.clear().opacity;
            regionObj["clear"] = clearObj;

            // Status flags
            regionObj["enabled"] = region.isEnabled();
            regionObj["locked"] = region.isLocked();
            regionObj["translationVisible"] = region.isTranslationVisible();
            regionObj["isCleared"] = region.isCleared();
            regionObj["contentRevision"] = static_cast<qint64>(region.contentRevision());

            regionsArr.append(regionObj);
        }
        pageObj["regions"] = regionsArr;
        pagesArr.append(pageObj);
    }
    root["pages"] = pagesArr;

    return root;
}

QByteArray ProjectSerializer::serialize(const domain::ComicProject& project) {
    const QJsonObject obj = serializeToJson(project);
    const QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Indented);
}

std::expected<domain::ComicProject, domain::ValidationError>
ProjectSerializer::deserialize(const QByteArray& jsonData) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(domain::ValidationError{
            .code = domain::ProjectErrorCode::InvalidStructure,
            .message = std::format("JSON parse error: {}", parseError.errorString().toStdString()),
            .path = ""});
    }

    return deserializeFromJson(doc.object());
}

std::expected<domain::ComicProject, domain::ValidationError> ProjectSerializer::deserialize(std::string_view jsonStr) {
    return deserialize(QByteArray(jsonStr.data(), static_cast<qsizetype>(jsonStr.size())));
}

std::expected<domain::ComicProject, domain::ValidationError>
ProjectSerializer::deserializeFromJson(const QJsonObject& rawObj) {
    // Step 1: Pre-migration envelope check
    // Only validates version integers; no field-content checks, so old schema files can proceed.
    const auto envelopeRes = ProjectSchemaValidator::validateEnvelope(rawObj);
    if (!envelopeRes) {
        return std::unexpected(envelopeRes.error());
    }

    // Step 2: Migration
    auto migratedRes = ProjectMigrator::migrateIfNeeded(rawObj);
    if (!migratedRes) {
        return std::unexpected(migratedRes.error());
    }
    const QJsonObject& root = *migratedRes;

    // Step 3: Full structural validation (post-migration, current schema)
    const auto schemaRes = ProjectSchemaValidator::validateFull(root);
    if (!schemaRes) {
        return std::unexpected(schemaRes.error());
    }

    // Step 4: Construct domain objects
    const auto projIdRes = domain::ProjectId::parse(root["id"].toString().toStdString());
    if (!projIdRes) {
        return std::unexpected(domain::ValidationError{
            .code = domain::ProjectErrorCode::InvalidId, .message = "Invalid project ID UUID string", .path = "id"});
    }

    const auto srcLangRes = domain::LanguageCode::fromString(root["sourceLanguage"].toString().toStdString());
    if (!srcLangRes) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidSemanticData,
                                                       .message = "Invalid sourceLanguage code format",
                                                       .path = "sourceLanguage"});
    }

    const auto tgtLangRes = domain::LanguageCode::fromString(root["targetLanguage"].toString().toStdString());
    if (!tgtLangRes) {
        return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidSemanticData,
                                                       .message = "Invalid targetLanguage code format",
                                                       .path = "targetLanguage"});
    }

    const auto readDirOpt = stringToReadingDirection(root["readingDirection"].toString());
    if (!readDirOpt) {
        return std::unexpected(
            domain::ValidationError{.code = domain::ProjectErrorCode::InvalidStructure,
                                    .message = "Invalid readingDirection value (must be RightToLeft or LeftToRight)",
                                    .path = "readingDirection"});
    }

    domain::ComicProject project(root["name"].toString().toStdString(), *projIdRes);
    project.setProjectVersion(root["projectVersion"].toString().toStdString());
    project.setSchemaVersion(root["schemaVersion"].toInt());
    project.setFormatVersion(root["formatVersion"].toInt());
    // Each setter below marks the project dirty while a persisted field is applied, so a
    // completed load clears the flag once construction finishes.
    project.setSourceLanguage(*srcLangRes);
    project.setTargetLanguage(*tgtLangRes);
    project.setReadingDirection(*readDirOpt);

    // Settings
    if (root.contains("settings") && root["settings"].isObject()) {
        const QJsonObject sObj = root["settings"].toObject();
        domain::ProjectSettings settings;
        if (sObj.contains("defaultFontFamily") && sObj["defaultFontFamily"].isString()) {
            settings.defaultFontFamily = sObj["defaultFontFamily"].toString().toStdString();
        }
        if (sObj.contains("defaultFontSizePt") && sObj["defaultFontSizePt"].isDouble()) {
            settings.defaultFontSizePt = sObj["defaultFontSizePt"].toDouble();
        }
        if (sObj.contains("defaultOrientation") && sObj["defaultOrientation"].isString()) {
            if (auto o = stringToOrientation(sObj["defaultOrientation"].toString())) {
                settings.defaultOrientation = *o;
            }
        }
        if (sObj.contains("readingDirection") && sObj["readingDirection"].isString()) {
            if (auto r = stringToReadingDirection(sObj["readingDirection"].toString())) {
                settings.readingDirection = *r;
            }
        }
        if (sObj.contains("ocrEngine") && sObj["ocrEngine"].isString()) {
            settings.ocrEngine = sObj["ocrEngine"].toString().toStdString();
        }
        if (sObj.contains("translatorEngine") && sObj["translatorEngine"].isString()) {
            settings.translatorEngine = sObj["translatorEngine"].toString().toStdString();
        }
        project.setSettings(settings);
    }

    // Glossary
    if (root.contains("glossary") && root["glossary"].isArray()) {
        const QJsonArray gArr = root["glossary"].toArray();
        for (size_t gIdx = 0; gIdx < static_cast<size_t>(gArr.size()); ++gIdx) {
            if (!gArr[static_cast<qsizetype>(gIdx)].isObject()) {
                continue;
            }
            const QJsonObject gObj = gArr[static_cast<qsizetype>(gIdx)].toObject();
            const auto gIdRes = domain::GlossaryId::parse(gObj["id"].toString().toStdString());
            if (!gIdRes) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                               .message = "Invalid glossary entry UUID",
                                                               .path = std::format("glossary[{}].id", gIdx)});
            }
            domain::GlossaryEntry gEntry;
            gEntry.id = *gIdRes;
            gEntry.source = gObj["source"].toString().toStdString();
            gEntry.target = gObj["target"].toString().toStdString();
            gEntry.category = gObj["category"].toString().toStdString();
            gEntry.caseSensitive = gObj["caseSensitive"].toBool(false);
            gEntry.enabled = gObj.contains("enabled") ? gObj["enabled"].toBool(true) : true;

            if (!project.addGlossaryEntry(gEntry)) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::DuplicateId,
                                                               .message = "Duplicate glossary entry ID",
                                                               .path = std::format("glossary[{}].id", gIdx),
                                                               .entityId = gEntry.id.toString()});
            }
        }
    }

    // Pages. use loadPage/loadRegion to preserve and validate orderIndex values.
    // validateFull() already confirmed orderIndex == arrayIndex for all pages and regions.
    const QJsonArray pagesArr = root["pages"].toArray();
    for (size_t pIdx = 0; pIdx < static_cast<size_t>(pagesArr.size()); ++pIdx) {
        const QJsonObject pObj = pagesArr[static_cast<qsizetype>(pIdx)].toObject();
        const std::string pagePath = std::format("pages[{}]", pIdx);

        const auto pageIdRes = domain::PageId::parse(pObj["id"].toString().toStdString());
        if (!pageIdRes) {
            return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                           .message = "Invalid page ID UUID string",
                                                           .path = pagePath + ".id"});
        }

        const QJsonObject sizeObj = pObj["imageSize"].toObject();
        const domain::ImageSize imgSize{.width = sizeObj["width"].toInt(), .height = sizeObj["height"].toInt()};

        domain::ComicPage page(
            *pageIdRes, pObj["originalAsset"].toString().toStdString(), imgSize, pObj["orderIndex"].toInt());

        if (pObj.contains("layers") && pObj["layers"].isObject()) {
            const QJsonObject lObj = pObj["layers"].toObject();
            if (lObj.contains("clear") && lObj["clear"].isString()) {
                page.setClearLayerAsset(lObj["clear"].toString().toStdString());
            }
            if (lObj.contains("translation") && lObj["translation"].isString()) {
                page.setTranslationLayerAsset(lObj["translation"].toString().toStdString());
            }
            if (lObj.contains("manualOverride") && lObj["manualOverride"].isString()) {
                page.setManualOverrideLayerAsset(lObj["manualOverride"].toString().toStdString());
            }
        }

        // Regions. use loadRegion to preserve orderIndex without auto-reassignment.
        const QJsonArray rArr = pObj["regions"].toArray();
        for (size_t rIdx = 0; rIdx < static_cast<size_t>(rArr.size()); ++rIdx) {
            const QJsonObject rObj = rArr[static_cast<qsizetype>(rIdx)].toObject();
            const std::string regionPath = std::format("{}.regions[{}]", pagePath, rIdx);

            const auto regIdRes = domain::RegionId::parse(rObj["id"].toString().toStdString());
            if (!regIdRes) {
                return std::unexpected(domain::ValidationError{.code = domain::ProjectErrorCode::InvalidId,
                                                               .message = "Invalid region ID UUID string",
                                                               .path = regionPath + ".id"});
            }

            // Geometry
            const QJsonObject geoObj = rObj["geometry"].toObject();
            domain::RectGeometry geometry{.x = geoObj["x"].toDouble(),
                                          .y = geoObj["y"].toDouble(),
                                          .width = geoObj["width"].toDouble(),
                                          .height = geoObj["height"].toDouble()};

            domain::TextRegion region(*regIdRes, geometry, rObj["orderIndex"].toInt());

            if (rObj.contains("bubbleGeometry") && rObj["bubbleGeometry"].isObject()) {
                const QJsonObject bgObj = rObj["bubbleGeometry"].toObject();
                const double bx = bgObj["x"].toDouble();
                const double by = bgObj["y"].toDouble();
                const double bw = bgObj["width"].toDouble();
                const double bh = bgObj["height"].toDouble();
                // Non-finite coordinates are dropped rather than poisoning the domain model.
                if (std::isfinite(bx) && std::isfinite(by) && std::isfinite(bw) && std::isfinite(bh)) {
                    region.setBubbleGeometry(domain::RectGeometry{.x = bx, .y = by, .width = bw, .height = bh});
                }
            }
            if (rObj.contains("bubblePolygon") && rObj["bubblePolygon"].isArray()) {
                domain::PolygonGeometry poly;
                const QJsonArray polyArr = rObj["bubblePolygon"].toArray();
                for (const auto& v : polyArr) {
                    if (!v.isObject()) {
                        continue;
                    }
                    const QJsonObject ptObj = v.toObject();
                    const double px = ptObj["x"].toDouble();
                    const double py = ptObj["y"].toDouble();
                    if (!std::isfinite(px) || !std::isfinite(py)) {
                        continue;
                    }
                    poly.points.push_back({px, py});
                }
                if (poly.isValid()) {
                    region.setBubblePolygon(std::move(poly));
                }
            }

            // Source
            const QJsonObject srcObj = rObj["source"].toObject();
            domain::TextContent source;
            source.text = srcObj["text"].toString().toStdString();
            if (auto st = stringToSourceType(srcObj["sourceType"].toString())) {
                source.sourceType = *st;
            }
            source.confidence = srcObj["confidence"].toDouble(1.0);
            source.ocrEngine = srcObj["ocrEngine"].toString().toStdString();
            source.revision = static_cast<uint64_t>(srcObj["revision"].toInteger(0));
            region.setSource(source);

            // Translation
            const QJsonObject transObj = rObj["translation"].toObject();
            domain::TranslationEntry translation;
            if (transObj.contains("modelText") && transObj["modelText"].isString()) {
                translation.setModelResult(transObj["modelText"].toString().toStdString());
            }
            if (transObj.contains("candidates") && transObj["candidates"].isArray()) {
                const QJsonArray candArr = transObj["candidates"].toArray();
                for (const auto& cVal : candArr) {
                    if (cVal.isObject()) {
                        const QJsonObject cObj = cVal.toObject();
                        translation.addCandidate({.id = cObj["id"].toString().toStdString(),
                                                  .text = cObj["text"].toString().toStdString(),
                                                  .confidence = cObj["confidence"].toDouble(1.0),
                                                  .provider = cObj["provider"].toString().toStdString()});
                    }
                }
            }
            if (transObj.contains("selectedCandidateId") && !transObj["selectedCandidateId"].isNull()) {
                const std::string selId = transObj["selectedCandidateId"].toString().toStdString();
                if (!selId.empty()) {
                    if (!translation.selectCandidate(selId)) {
                        return std::unexpected(domain::ValidationError{
                            .code = domain::ProjectErrorCode::InvalidCandidateSelection,
                            .message = std::format("selectedCandidateId '{}' does not exist in candidates list", selId),
                            .path = regionPath + ".translation.selectedCandidateId",
                            .entityId = region.id().toString()});
                    }
                }
            }
            if (transObj.contains("manualText") && transObj["manualText"].isString()) {
                translation.setManualText(transObj["manualText"].toString().toStdString());
            }
            region.setTranslation(translation);

            // Layout
            const QJsonObject layObj = rObj["layout"].toObject();
            domain::TextLayout layout;
            layout.fontFamily = layObj["fontFamily"].toString().toStdString();
            layout.fontSizePt = layObj["fontSizePt"].toDouble();
            layout.fontWeight = layObj["fontWeight"].toInt(400);
            layout.letterSpacingEm = layObj["letterSpacingEm"].toDouble(0.0);
            layout.textColorHex = layObj["textColorHex"].toString("#000000").toStdString();
            layout.strokeColorHex = layObj["strokeColorHex"].toString("#FFFFFF").toStdString();
            layout.strokeWidthPx = layObj["strokeWidthPx"].toDouble(0.0);
            if (auto o = stringToOrientation(layObj["orientation"].toString())) {
                layout.orientation = *o;
            }
            if (layObj.contains("alignment") && layObj["alignment"].isObject()) {
                const QJsonObject alObj = layObj["alignment"].toObject();
                if (auto h = stringToHorizontalAlign(alObj["horizontal"].toString())) {
                    layout.alignment.horizontal = *h;
                }
                if (auto v = stringToVerticalAlign(alObj["vertical"].toString())) {
                    layout.alignment.vertical = *v;
                }
            }
            layout.lineSpacing = layObj["lineSpacing"].toDouble(1.2);
            if (layObj.contains("margins") && layObj["margins"].isObject()) {
                const QJsonObject mObj = layObj["margins"].toObject();
                layout.margins.top = mObj["top"].toDouble(0.0);
                layout.margins.right = mObj["right"].toDouble(0.0);
                layout.margins.bottom = mObj["bottom"].toDouble(0.0);
                layout.margins.left = mObj["left"].toDouble(0.0);
            }
            layout.wordWrap = layObj["wordWrap"].toBool(true);
            layout.autoFit = layObj["autoFit"].toBool(false);
            layout.minFontSizePt = layObj["minFontSizePt"].toDouble(6.0);
            layout.maxFontSizePt = layObj["maxFontSizePt"].toDouble(72.0);
            layout.rotationDeg = layObj["rotationDeg"].toDouble(0.0);
            region.setLayout(layout);

            // Clear
            const QJsonObject clrObj = rObj["clear"].toObject();
            domain::ClearSettings clear;
            clear.enabled = clrObj["enabled"].toBool(true);
            clear.colorHex = clrObj["colorHex"].toString("#FFFFFF").toStdString();
            clear.opacity = clrObj["opacity"].toDouble(1.0);
            region.setClear(clear);

            // Status flags
            region.setEnabled(rObj["enabled"].toBool(true));
            region.setLocked(rObj["locked"].toBool(false));
            region.setTranslationVisible(rObj["translationVisible"].toBool(true));
            region.setIsCleared(rObj["isCleared"].toBool(false));
            region.setContentRevision(static_cast<uint64_t>(rObj["contentRevision"].toInteger(0)));

            // loadRegion: preserves orderIndex, validates position == rIdx.
            if (!domain::PageRehydrationBuilder(page).loadRegion(std::move(region))) {
                return std::unexpected(domain::ValidationError{
                    .code = domain::ProjectErrorCode::InvalidSemanticData,
                    .message = "Failed to load region into page (invalid geometry, out-of-bounds, "
                               "duplicate ID, or orderIndex mismatch)",
                    .path = regionPath,
                    .entityId = region.id().toString()});
            }
        }

        // loadPage: preserves orderIndex, validates position == pIdx.
        if (!domain::ProjectRehydrationBuilder(project).loadPage(std::move(page))) {
            return std::unexpected(domain::ValidationError{
                .code = domain::ProjectErrorCode::DuplicateId,
                .message = "Failed to load page into project (duplicate ID, invalid page structure, "
                           "or orderIndex mismatch)",
                .path = pagePath,
                .entityId = page.id().toString()});
        }
    }

    // Step 5: Semantic Validation
    const auto semRes = domain::SemanticValidator::validate(project);
    if (!semRes) {
        return std::unexpected(semRes.error().front());
    }

    // Step 6: Mark clean, because a freshly loaded aggregate carries no unsaved edits.
    project.markClean();

    return project;
}

} // namespace manga::data
