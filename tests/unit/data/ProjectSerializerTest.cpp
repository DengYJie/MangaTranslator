#include "ProjectSerializerTest.h"

#include "data/project/ProjectSerializer.h"
#include "domain/model/ComicProject.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace manga::domain;
using namespace manga::data;

namespace {

// Base valid region JSON (alignment + margins included)
constexpr const char* kValidRegionJson = R"({
    "id": "123e4567-e89b-42d3-a456-426614174002",
    "orderIndex": 0,
    "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
    "source": { "text": "", "sourceType": "PureManual" },
    "translation": {},
    "layout": {
        "fontFamily": "Sans",
        "fontSizePt": 12,
        "orientation": "Horizontal",
        "lineSpacing": 1.2,
        "alignment": { "horizontal": "Center", "vertical": "Middle" },
        "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
    },
    "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
})";

// Wraps a JSON string into a minimal valid project with one page containing the given region.
// regionJson: the JSON for one region, or empty string for no regions.
// pageOrderIndex: orderIndex of the page.
std::string
wrapInProject(const std::string& regionJson, int pageOrderIndex = 0, const std::string& extraRootFields = "") {
    const std::string regions = regionJson.empty() ? "" : regionJson;
    const std::string regionsArray = regions.empty() ? "[]" : ("[" + regions + "]");

    return R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",)" +
           extraRootFields + R"(
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": )" +
           std::to_string(pageOrderIndex) + R"(,
                "regions": )" +
           regionsArray + R"(
            }
        ]
    })";
}

std::string minimalProject(const std::string& extraRootFields = "") {
    return R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",)" +
           extraRootFields + R"(
        "pages": []
    })";
}

ComicProject createFullTestProject() {
    ComicProject project("Manga Test Project");
    project.setSourceLanguage(languages::Japanese);
    project.setTargetLanguage(languages::SimplifiedChinese);
    project.setReadingDirection(ReadingDirection::RightToLeft);

    ProjectSettings settings;
    settings.defaultFontFamily = "Noto Sans CJK JP";
    settings.defaultFontSizePt = 16.0;
    settings.defaultOrientation = TextOrientation::Vertical;
    settings.readingDirection = ReadingDirection::RightToLeft;
    settings.ocrEngine = "manga-ocr";
    settings.translatorEngine = "openai-gpt4o";
    project.setSettings(settings);

    // Glossary with enabled = false
    GlossaryEntry g1;
    g1.id = GlossaryId::generate();
    g1.source = "せんぱい";
    g1.target = "学长";
    g1.category = "Title";
    g1.caseSensitive = false;
    g1.enabled = false;
    project.addGlossaryEntry(g1);

    ComicPage page1(PageId::generate(), "assets/original/page_0001.png", {.width = 1200, .height = 1800}, 0);
    page1.setClearLayerAsset("assets/derived/" + page1.id().toString() + "/clear.png");
    page1.setTranslationLayerAsset("assets/derived/" + page1.id().toString() + "/trans.png");

    TextRegion r1(RegionId::generate(), {.x = 100.0, .y = 150.0, .width = 250.0, .height = 400.0}, 0);
    TextContent src1;
    src1.setOcrResult("待たせたな！", 0.98, "manga-ocr");
    r1.setSource(src1);

    TranslationEntry trans1;
    trans1.setModelResult("让你久等了！",
                          {{.id = "c1", .text = "让你久等了！", .confidence = 0.95, .provider = "openai"},
                           {.id = "c2", .text = "久等了！", .confidence = 0.85, .provider = "openai"}});
    trans1.setManualText("让你久等了，少年！");
    r1.setTranslation(trans1);

    TextLayout layout1;
    layout1.fontFamily = "Noto Sans CJK SC";
    layout1.fontSizePt = 18.0;
    layout1.orientation = TextOrientation::Vertical;
    layout1.alignment = {HorizontalAlignment::Center, VerticalAlignment::Middle};
    layout1.lineSpacing = 1.3;
    layout1.margins = {4.0, 4.0, 4.0, 4.0};
    layout1.wordWrap = true;
    layout1.autoFit = true;
    layout1.minFontSizePt = 10.0;
    layout1.maxFontSizePt = 32.0;
    r1.setLayout(layout1);

    ClearSettings clear1;
    clear1.enabled = true;
    clear1.colorHex = "#FFFFFF";
    clear1.opacity = 1.0;
    r1.setClear(clear1);

    r1.setEnabled(true);
    r1.setLocked(false);
    r1.setTranslationVisible(true);
    r1.setIsCleared(true);
    r1.setContentRevision(3);

    page1.addRegion(r1);
    project.addPage(page1);

    return project;
}

QJsonObject parseObject(const std::string& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

template <typename Func> QJsonObject mutateFirstPage(QJsonObject root, Func&& mutate) {
    QJsonArray pages = root["pages"].toArray();
    QJsonObject page = pages[0].toObject();
    mutate(page);
    pages[0] = page;
    root["pages"] = pages;
    return root;
}

template <typename Func> QJsonObject mutateFirstRegion(QJsonObject root, Func&& mutate) {
    return mutateFirstPage(std::move(root), [&](QJsonObject& page) {
        QJsonArray regions = page["regions"].toArray();
        QJsonObject region = regions[0].toObject();
        mutate(region);
        regions[0] = region;
        page["regions"] = regions;
    });
}

bool rejectsAsInvalidStructure(const QJsonObject& root) {
    const auto result = ProjectSerializer::deserialize(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return !result.has_value() && result.error().code == ProjectErrorCode::InvalidStructure;
}

} // namespace

void ProjectSerializerTest::testMinimalProjectRoundTrip() {
    ComicProject original("Minimal Project");
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 800, .height = 1200}, 0);

    TextRegion region(RegionId::generate(), {.x = 50.0, .y = 50.0, .width = 200.0, .height = 100.0}, 0);
    TextContent src;
    src.setPureManual("Sample text");
    region.setSource(src);

    TranslationEntry trans;
    trans.setModelResult("示例文字");
    region.setTranslation(trans);

    page.addRegion(region);
    original.addPage(page);

    const QByteArray json = ProjectSerializer::serialize(original);
    QVERIFY(!json.isEmpty());

    const auto result = ProjectSerializer::deserialize(json);
    QVERIFY2(result.has_value(), result ? "" : result.error().toString().c_str());

    const ComicProject& deserialized = *result;
    QCOMPARE(deserialized.id(), original.id());
    QCOMPARE(deserialized.name(), original.name());
    QCOMPARE(deserialized.pages().size(), original.pages().size());
    QCOMPARE(deserialized.pages().front().regions().size(), original.pages().front().regions().size());

    const auto& origReg = original.pages().front().regions().front();
    const auto& desReg = deserialized.pages().front().regions().front();
    QCOMPARE(desReg.id(), origReg.id());
    QCOMPARE(desReg.geometry(), origReg.geometry());
    QCOMPARE(QString::fromStdString(desReg.translation().effectiveText()), "示例文字");

    QVERIFY(original == deserialized);
}

void ProjectSerializerTest::testFullProjectRoundTripWithDisabledGlossary() {
    const ComicProject original = createFullTestProject();
    QCOMPARE(original.glossary().front().enabled, false);

    const QByteArray json = ProjectSerializer::serialize(original);
    QVERIFY(!json.isEmpty());

    const auto result = ProjectSerializer::deserialize(json);
    QVERIFY2(result.has_value(), result ? "" : result.error().toString().c_str());

    const ComicProject& deserialized = *result;
    QVERIFY(original == deserialized);
    QCOMPARE(deserialized.glossary().front().enabled, false);
    QCOMPARE(QString::fromStdString(deserialized.pages()[0].regions()[0].translation().effectiveText()),
             "让你久等了，少年！");
}

void ProjectSerializerTest::testLoadedProjectIsClean() {
    // Serialize a dirty project, then verify the deserialized project is clean.
    ComicProject original("Dirty Project");
    original.setName("Modified Name"); // marks dirty
    QVERIFY(original.isDirty());

    const QByteArray json = ProjectSerializer::serialize(original);

    const auto result = ProjectSerializer::deserialize(json);
    QVERIFY2(result.has_value(), result ? "" : result.error().toString().c_str());

    // Invariant: a successfully loaded project must start clean
    QVERIFY(!result->isDirty());
}

void ProjectSerializerTest::testPersistedDirtyFieldIsRejected() {
    const auto result = ProjectSerializer::deserialize(minimalProject(R"("isDirty": true,)"));
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testOptionalSchemaFieldsAreStrictlyValidated() {
    QJsonObject invalidSettings = parseObject(minimalProject());
    invalidSettings["settings"] = QJsonObject{{"defaultOrientation", "Diagonal"}};
    QVERIFY(rejectsAsInvalidStructure(invalidSettings));

    const QJsonObject base = parseObject(wrapInProject(kValidRegionJson));

    const QJsonObject invalidLayers =
        mutateFirstPage(base, [](QJsonObject& page) { page["layers"] = QJsonObject{{"clear", 42}}; });
    QVERIFY(rejectsAsInvalidStructure(invalidLayers));

    const QJsonObject invalidSourceRevision = mutateFirstRegion(base, [](QJsonObject& region) {
        QJsonObject source = region["source"].toObject();
        source["revision"] = -1;
        region["source"] = source;
    });
    QVERIFY(rejectsAsInvalidStructure(invalidSourceRevision));

    const QJsonObject invalidCandidates = mutateFirstRegion(base, [](QJsonObject& region) {
        QJsonObject translation = region["translation"].toObject();
        translation["candidates"] = QJsonObject{};
        region["translation"] = translation;
    });
    QVERIFY(rejectsAsInvalidStructure(invalidCandidates));

    const QJsonObject invalidOrientation = mutateFirstRegion(base, [](QJsonObject& region) {
        QJsonObject layout = region["layout"].toObject();
        layout["orientation"] = "Diagonal";
        region["layout"] = layout;
    });
    QVERIFY(rejectsAsInvalidStructure(invalidOrientation));

    const QJsonObject invalidFlag = mutateFirstRegion(base, [](QJsonObject& region) { region["enabled"] = "yes"; });
    QVERIFY(rejectsAsInvalidStructure(invalidFlag));
}

void ProjectSerializerTest::testMalformedJson() {
    const auto res = ProjectSerializer::deserialize(std::string_view("{ invalid json }"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testMissingVersionFields() {
    const std::string missingVer = R"({
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": []
    })";
    const auto res = ProjectSerializer::deserialize(missingVer);
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testUnsupportedSchemaVersion() {
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 99,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": []
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::UnsupportedSchemaVersion);
}

void ProjectSerializerTest::testUnsupportedFormatVersion() {
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 99,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": []
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::UnsupportedFormatVersion);
}

void ProjectSerializerTest::testAdditionalPropertiesRejection() {
    // Root level unknown property
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "unknownProperty": "disallowed",
        "pages": []
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testSettingsFieldTypeError() {
    // defaultFontSizePt must be a number > 0, not a string
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "settings": { "defaultFontSizePt": "twelve" },
        "pages": []
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testPageOrderIndexMismatch() {
    // Page at array position 0 has orderIndex 1. must be rejected
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 1,
                "regions": []
            }
        ]
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::DuplicateOrderIndex);
}

void ProjectSerializerTest::testRegionOrderIndexMismatch() {
    // Region at array position 0 has orderIndex 2. must be rejected
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 2,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::DuplicateOrderIndex);
}

void ProjectSerializerTest::testPageOrderIndexDuplicate() {
    // Two pages both with orderIndex 0. second one should fail (position 1 but index 0)
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 0,
                "regions": []
            },
            {
                "id": "123e4567-e89b-42d3-a456-426614174002",
                "originalAsset": "assets/original/p2.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 0,
                "regions": []
            }
        ]
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::DuplicateOrderIndex);
}

void ProjectSerializerTest::testRegionOrderIndexDuplicate() {
    // Two regions both with orderIndex 0. second one at position 1 should fail
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 0,
                "regions": [
                    {
                        "id": "123e4567-e89b-42d3-a456-426614174002",
                        "orderIndex": 0,
                        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
                        "source": { "text": "", "sourceType": "PureManual" },
                        "translation": {},
                        "layout": { "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal",
                                    "lineSpacing": 1.2,
                                    "alignment": { "horizontal": "Center", "vertical": "Middle" },
                                    "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 } },
                        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
                    },
                    {
                        "id": "123e4567-e89b-42d3-a456-426614174003",
                        "orderIndex": 0,
                        "geometry": { "type": "rect", "x": 200, "y": 10, "width": 100, "height": 100 },
                        "source": { "text": "", "sourceType": "PureManual" },
                        "translation": {},
                        "layout": { "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal",
                                    "lineSpacing": 1.2,
                                    "alignment": { "horizontal": "Center", "vertical": "Middle" },
                                    "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 } },
                        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
                    }
                ]
            }
        ]
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::DuplicateOrderIndex);
}

void ProjectSerializerTest::testInvalidGeometryType() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "polygon", "x": 0, "y": 0, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testOutOfBoundsGeometryInJson() {
    // Region at x=450, width=100 exceeds imageSize.width=500
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 500, "height": 500 },
                "orderIndex": 0,
                "regions": [
                    {
                        "id": "123e4567-e89b-42d3-a456-426614174002",
                        "orderIndex": 0,
                        "geometry": { "type": "rect", "x": 450, "y": 10, "width": 100, "height": 100 },
                        "source": { "text": "", "sourceType": "PureManual" },
                        "translation": {},
                        "layout": {
                            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
                            "alignment": { "horizontal": "Center", "vertical": "Middle" },
                            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
                        },
                        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
                    }
                ]
            }
        ]
    })"));
    QVERIFY(!res.has_value());
    // Must fail. out-of-bounds geometry is rejected.
    QVERIFY(res.error().code == ProjectErrorCode::OutOfBoundsGeometry ||
            res.error().code == ProjectErrorCode::InvalidSemanticData);
}

void ProjectSerializerTest::testGeometryUnknownFieldRejected() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100, "unknownField": 42 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testSourceUnknownFieldRejected() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual", "unknownSourceField": true },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testSourceConfidenceOutOfRange() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual", "confidence": 1.5 },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testTranslationUnknownFieldRejected() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": { "unknownTransField": "bad" },
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testCandidateMissingIdOrText() {
    // Candidate missing 'id'
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": { "candidates": [ { "text": "candidate without id" } ] },
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testCandidateConfidenceOutOfRange() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": { "candidates": [ { "id": "c1", "text": "ok", "confidence": -0.1 } ] },
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testLayoutMissingAlignment() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testLayoutMissingMargins() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testLayoutUnknownFieldRejected() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "", "sourceType": "PureManual" },
        "translation": {},
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 },
            "unknownLayoutField": "bad"
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidStructure);
}

void ProjectSerializerTest::testDuplicateIdsInJson() {
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-42d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": [
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p1.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 0,
                "regions": []
            },
            {
                "id": "123e4567-e89b-42d3-a456-426614174001",
                "originalAsset": "assets/original/p2.png",
                "imageSize": { "width": 1000, "height": 1000 },
                "orderIndex": 1,
                "regions": []
            }
        ]
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::DuplicateId);
}

void ProjectSerializerTest::testInvalidCandidateSelectionInJson() {
    const auto res = ProjectSerializer::deserialize(wrapInProject(R"({
        "id": "123e4567-e89b-42d3-a456-426614174002",
        "orderIndex": 0,
        "geometry": { "type": "rect", "x": 10, "y": 10, "width": 100, "height": 100 },
        "source": { "text": "原文", "sourceType": "PureManual" },
        "translation": {
            "modelText": "Model text",
            "selectedCandidateId": "non_existent_candidate_id",
            "candidates": [ { "id": "valid_c1", "text": "Candidate 1" } ]
        },
        "layout": {
            "fontFamily": "Sans", "fontSizePt": 12, "orientation": "Horizontal", "lineSpacing": 1.2,
            "alignment": { "horizontal": "Center", "vertical": "Middle" },
            "margins": { "top": 0, "right": 0, "bottom": 0, "left": 0 }
        },
        "clear": { "enabled": true, "colorHex": "#FFFFFF", "opacity": 1.0 }
    })"));
    QVERIFY(!res.has_value());
    // Must NOT silently drop selectedCandidateId; must fail with InvalidCandidateSelection
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidCandidateSelection);
}

void ProjectSerializerTest::testNonV4UuidInJson() {
    // UUID with version 1 (123e4567-e89b-12d3-a456-426614174000)
    const auto res = ProjectSerializer::deserialize(std::string_view(R"({
        "projectVersion": "0.1.0",
        "schemaVersion": 1,
        "formatVersion": 1,
        "id": "123e4567-e89b-12d3-a456-426614174000",
        "name": "Test",
        "sourceLanguage": "ja",
        "targetLanguage": "zh-Hans",
        "readingDirection": "RightToLeft",
        "pages": []
    })"));
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, ProjectErrorCode::InvalidId);
}
