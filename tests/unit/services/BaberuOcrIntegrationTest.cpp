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
    return QDir(QStringLiteral(MANGA_OCR_FIXTURES_DIR)).absoluteFilePath(QStringLiteral(".."));
}

} // namespace

BaberuOcrIntegrationTest::BaberuOcrIntegrationTest() = default;

BaberuOcrIntegrationTest::~BaberuOcrIntegrationTest() = default;

void BaberuOcrIntegrationTest::initTestCase() {
    if (modelDir().isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set; skipping integration tests");
    }

    m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Test");
    m_executor = std::make_unique<BoundedTaskExecutor>(1);
    m_engine = std::make_unique<OnnxMangaOcrEngine>(m_env.get(), m_executor.get(), modelDir());
}

void BaberuOcrIntegrationTest::cleanupTestCase() {
    m_engine.reset();
    m_executor.reset();
    m_env.reset();
}

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

    QString path = QDir(fixtureDir()).filePath(QStringLiteral("preprocess/solid_red_2x2.png"));
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

    QString path = QDir(fixtureDir()).filePath(QStringLiteral("preprocess/gradient_4x3.png"));
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

void BaberuOcrIntegrationTest::integrationEngineRecognizesText_data() {
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("expectedText");

    QFile expectedFile(QDir(fixtureDir()).filePath(QStringLiteral("samples/expected.txt")));
    QVERIFY2(expectedFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(expectedFile.errorString()));
    while (!expectedFile.atEnd()) {
        const QString line = QString::fromUtf8(expectedFile.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype separator = line.indexOf(QLatin1Char('\t'));
        QVERIFY2(separator > 0, qPrintable(QStringLiteral("Invalid OCR expectation: %1").arg(line)));
        const QString fileName = line.left(separator);
        const QString expectedText = line.mid(separator + 1);
        QTest::newRow(qPrintable(fileName)) << fileName << expectedText;
    }
}

void BaberuOcrIntegrationTest::integrationEngineRecognizesText() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }

    QFETCH(QString, fileName);
    QFETCH(QString, expectedText);

    const QString path = QDir(fixtureDir()).filePath(QStringLiteral("samples/") + fileName);
    QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("Missing OCR fixture: %1").arg(path)));
    QImage img(path);
    QVERIFY(!img.isNull());

    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    req.rect = domain::RectGeometry{0.0, 0.0, static_cast<double>(img.width()), static_cast<double>(img.height())};

    application::OperationContext context;
    auto future = m_engine->recognize(req, context);
    future.waitForFinished();
    auto res = future.result();

    if (!res.has_value()) {
        QFAIL(qPrintable(res.error().safeMessage));
    }
    QCOMPARE(res->regionId, req.regionId);
    QCOMPARE(res->text, expectedText);
}

} // namespace manga::services::test
