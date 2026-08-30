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
#include <QStringList>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#include <vector>

namespace manga::services::test {

namespace {

static QString modelDir() {
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("MANGA_OCR_MODEL_DIR"));
}

static QString fixtureDir() {
    return QDir(QStringLiteral(MANGA_OCR_FIXTURES_DIR)).absoluteFilePath(QStringLiteral(".."));
}

static qsizetype unicodeEditDistance(const QString& actual, const QString& expected) {
    const auto actualCodePoints = actual.toUcs4();
    const auto expectedCodePoints = expected.toUcs4();
    std::vector<qsizetype> previous(static_cast<std::size_t>(expectedCodePoints.size() + 1));
    std::vector<qsizetype> current(previous.size());

    for (qsizetype column = 0; column <= expectedCodePoints.size(); ++column) {
        previous[static_cast<std::size_t>(column)] = column;
    }
    for (qsizetype row = 1; row <= actualCodePoints.size(); ++row) {
        current[0] = row;
        for (qsizetype column = 1; column <= expectedCodePoints.size(); ++column) {
            const qsizetype substitutionCost = actualCodePoints[row - 1] == expectedCodePoints[column - 1] ? 0 : 1;
            current[static_cast<std::size_t>(column)] =
                std::min({previous[static_cast<std::size_t>(column)] + 1,
                          current[static_cast<std::size_t>(column - 1)] + 1,
                          previous[static_cast<std::size_t>(column - 1)] + substitutionCost});
        }
        std::swap(previous, current);
    }
    return previous.back();
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

void BaberuOcrIntegrationTest::integrationEngineMeetsCorpusAccuracy() {
    const QString dir = modelDir();
    if (dir.isEmpty()) {
        QSKIP("MANGA_OCR_MODEL_DIR not set");
    }

    constexpr double kMaximumCorpusCer = 0.20;
    QFile expectedFile(QDir(fixtureDir()).filePath(QStringLiteral("samples/expected.txt")));
    QVERIFY2(expectedFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(expectedFile.errorString()));

    qsizetype totalDistance = 0;
    qsizetype totalExpectedCodePoints = 0;
    int sampleCount = 0;
    QStringList mismatchSummary;
    while (!expectedFile.atEnd()) {
        const QString line = QString::fromUtf8(expectedFile.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype separator = line.indexOf(QLatin1Char('\t'));
        QVERIFY2(separator > 0, qPrintable(QStringLiteral("Invalid OCR expectation: %1").arg(line)));
        const QString fileName = line.left(separator);
        const QString expectedText = line.mid(separator + 1);

        const QString path = QDir(fixtureDir()).filePath(QStringLiteral("samples/") + fileName);
        QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("Missing OCR fixture: %1").arg(path)));
        QImage image(path);
        QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Invalid OCR fixture: %1").arg(fileName)));

        application::OcrRequest request;
        request.pageId = domain::PageId::generate();
        request.regionId = domain::RegionId::generate();
        request.image = image;
        request.rect =
            domain::RectGeometry{0.0, 0.0, static_cast<double>(image.width()), static_cast<double>(image.height())};

        application::OperationContext context;
        auto future = m_engine->recognize(request, context);
        future.waitForFinished();
        auto response = future.result();

        if (!response.has_value()) {
            QFAIL(qPrintable(response.error().safeMessage));
        }
        QCOMPARE(response->regionId, request.regionId);
        QVERIFY2(!response->text.isEmpty(), qPrintable(QStringLiteral("Empty OCR result for %1").arg(fileName)));

        const qsizetype distance = unicodeEditDistance(response->text, expectedText);
        if (distance > 0) {
            mismatchSummary.append(QStringLiteral("%1:%2").arg(fileName).arg(distance));
        }
        totalDistance += distance;
        totalExpectedCodePoints += expectedText.toUcs4().size();
        ++sampleCount;
    }

    QCOMPARE(sampleCount, 8);
    QVERIFY(totalExpectedCodePoints > 0);
    const double corpusCer = static_cast<double>(totalDistance) / static_cast<double>(totalExpectedCodePoints);
    QVERIFY2(corpusCer <= kMaximumCorpusCer,
             qPrintable(QStringLiteral("OCR corpus CER %1 exceeds %2; distances: %3")
                            .arg(corpusCer, 0, 'f', 4)
                            .arg(kMaximumCorpusCer, 0, 'f', 2)
                            .arg(mismatchSummary.join(QStringLiteral(", ")))));
}

} // namespace manga::services::test
