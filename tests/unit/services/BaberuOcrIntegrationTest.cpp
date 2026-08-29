#include "BaberuOcrIntegrationTest.h"

#include "services/execution/BoundedTaskExecutor.h"
#include "services/ocr/BaberuManifest.h"
#include "services/ocr/BaberuOcrPreprocessor.h"
#include "services/ocr/BaberuVocab.h"
#include "services/ocr/OnnxMangaOcrEngine.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcessEnvironment>
#include <QtTest/QtTest>

#include <cmath>
#include <onnxruntime_cxx_api.h>

namespace manga::services::test {

namespace {

static QString modelDir() {
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("MANGA_OCR_MODEL_DIR"));
}

static QString fixtureDir() {
    return QStringLiteral(MANGA_OCR_FIXTURES_DIR);
}

} // namespace

void BaberuOcrIntegrationTest::initTestCase() {
    if (modelDir().isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set; skipping integration tests");
    }
}

void BaberuOcrIntegrationTest::cleanupTestCase() {}

void BaberuOcrIntegrationTest::integrationManifestLoadsAndValidates() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }
    auto result = loadAndValidateManifest(dir);
    QVERIFY2(result.has_value(), qPrintable(result.has_value() ? QString{} : result.error().safeMessage));
    QCOMPARE(result->manifestVersion, 1);
    QCOMPARE(result->id, QStringLiteral("genshiai-baberu-ocr-onnx"));
}

void BaberuOcrIntegrationTest::integrationPreprocessorMatchesPilGoldenSolidRed() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }
    auto manifestResult = loadAndValidateManifest(dir);
    QVERIFY(manifestResult.has_value());
    const auto& manifest = *manifestResult;

    QString path = QDir(fixtureDir()).filePath(QStringLiteral("solid_red_2x2.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found");
    }
    QImage img(path);
    QVERIFY(!img.isNull());

    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    req.rect = domain::RectGeometry{0.0, 0.0, static_cast<double>(img.width()), static_cast<double>(img.height())};

    auto result = BaberuOcrPreprocessor::preprocess(req, manifest);
    QVERIFY(result.has_value());
    QCOMPARE(static_cast<int>(result->size()), 1 * 3 * 224 * 224);

    // PIL golden values for solid red 2x2 -> 224x224 bicubic:
    // All pixels same. Check (0,0).
    constexpr float kTol = 1e-6f;
    const auto& t = *result;
    const int H = 224, W = 224;
    QVERIFY(std::abs(t[0 * H * W + 0] - 2.248908f) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 0] - (-2.035714f)) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 0] - (-1.804444f)) <= kTol);
}

void BaberuOcrIntegrationTest::integrationPreprocessorMatchesPilGoldenGradient() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }
    auto manifestResult = loadAndValidateManifest(dir);
    QVERIFY(manifestResult.has_value());
    const auto& manifest = *manifestResult;

    QString path = QDir(fixtureDir()).filePath(QStringLiteral("gradient_4x3.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found");
    }
    QImage img(path);
    QVERIFY(!img.isNull());

    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    req.rect = domain::RectGeometry{0.0, 0.0, static_cast<double>(img.width()), static_cast<double>(img.height())};

    auto result = BaberuOcrPreprocessor::preprocess(req, manifest);
    QVERIFY(result.has_value());

    constexpr float kTol = 1e-6f;
    const auto& t = *result;
    const int H = 224, W = 224;
    QVERIFY(std::abs(t[0 * H * W + 0] - (-2.117904f)) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 0] - (-2.035714f)) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 0] - (0.426493f)) <= kTol);
    QVERIFY(std::abs(t[0 * H * W + 112 * W + 112] - (-0.559551f)) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 112 * W + 112] - (-0.530112f)) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 112 * W + 112] - (0.426493f)) <= kTol);
}

void BaberuOcrIntegrationTest::integrationVocabLoadsCorrectly() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }
    auto manifestResult = loadAndValidateManifest(dir);
    QVERIFY(manifestResult.has_value());
    const auto& manifest = *manifestResult;

    QString vocabPath = QDir(dir).filePath(manifest.tokenizer.vocabFile);
    auto vocabResult = BaberuVocab::load(vocabPath,
                                         manifest.tokenizer.bosTokenId,
                                         manifest.tokenizer.eosTokenId,
                                         manifest.tokenizer.padTokenId,
                                         manifest.tokenizer.unkTokenId);
    QVERIFY(vocabResult.has_value());
    // Baberu vocab: 14626 chars + 4 special = 14630 total
    QCOMPARE(vocabResult->vocabSize(), 14630);
    // Content IDs must be non-empty
    QVERIFY(!vocabResult->contentIds().isEmpty());
}

void BaberuOcrIntegrationTest::integrationEngineRecognizesText() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Test");
    BoundedTaskExecutor executor(1);
    OnnxMangaOcrEngine engine(&env, &executor, dir);

    QString path = QDir(fixtureDir()).filePath(QStringLiteral("gradient_4x3.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found");
    }
    QImage img(path);
    QVERIFY(!img.isNull());

    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    req.rect = domain::RectGeometry{0.0, 0.0, static_cast<double>(img.width()), static_cast<double>(img.height())};

    application::OperationContext context;
    auto future = engine.recognize(req, context);
    future.waitForFinished();
    auto res = future.result();

    // We just verify it successfully ran inference and returned *something* without errors.
    QVERIFY2(res.has_value(), qPrintable(res.error().safeMessage));
}

} // namespace manga::services::test
