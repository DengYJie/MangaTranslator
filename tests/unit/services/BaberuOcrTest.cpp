#include "BaberuOcrTest.h"

#include "services/execution/BoundedTaskExecutor.h"
#include "services/ocr/BaberuManifest.h"
#include "services/ocr/BaberuOcrPreprocessor.h"
#include "services/ocr/BaberuVocab.h"
#include "services/ocr/OnnxMangaOcrEngine.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>
#include <onnxruntime_cxx_api.h>

namespace manga::services::test {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Fixture directory is injected via CMake at compile time.
static QString fixtureDir() {
    return QStringLiteral(MANGA_OCR_FIXTURES_DIR);
}

// Build a minimal valid manifest directory in a temp dir.
// Files are small dummy binaries whose SHA-256 we control.
static bool writeFakeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(data);
    return true;
}

static QByteArray sha256Of(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

static QString sha256HexOf(const QByteArray& data) {
    return QString::fromLatin1(sha256Of(data).toHex());
}

// Build a complete valid manifest directory. Returns the model dir path.
static QString buildValidManifestDir(QTemporaryDir& dir) {
    QDir d(dir.path());
    d.mkdir(QStringLiteral("tokenizer"));

    // Fake ONNX blobs
    const QByteArray visData = QByteArray(64, 'V');
    const QByteArray preData = QByteArray(64, 'P');
    const QByteArray stepData = QByteArray(64, 'S');
    writeFakeFile(d.filePath(QStringLiteral("vision.onnx")), visData);
    writeFakeFile(d.filePath(QStringLiteral("prefill.onnx")), preData);
    writeFakeFile(d.filePath(QStringLiteral("step.onnx")), stepData);

    // Fake vocab: JSON array of 2 characters
    const QByteArray vocabData = QByteArray(R"(["a","b"])");
    writeFakeFile(d.filePath(QStringLiteral("tokenizer/vocab.json")), vocabData);

    const QString visHash = sha256HexOf(visData);
    const QString preHash = sha256HexOf(preData);
    const QString stepHash = sha256HexOf(stepData);
    const QString vocHash = sha256HexOf(vocabData);

    const QString manifest = QString(R"({
  "manifestVersion": 1,
  "id": "test-model",
  "version": "1.0.0",
  "source": {"repository": "test/repo", "revision": "d9cc13153e9a1cd8fdfa3b7b1cc329da2020aeae", "license": "MIT"},
  "runtime": {"minimumOnnxRuntime": "1.20.0", "opset": 17, "executionProvider": "CPUExecutionProvider"},
  "graphs": [
    {"role": "vision",          "file": "vision.onnx",  "sha256": "%1", "inputs": ["pixel_values"], "outputs": ["vision_embeds"]},
    {"role": "prefill",         "file": "prefill.onnx", "sha256": "%2", "inputs": ["vision_embeds", "input_ids"], "outputs": ["logits"]},
    {"role": "step",            "file": "step.onnx",    "sha256": "%3", "inputs": ["input_ids", "position_ids"], "outputs": ["logits"]}
  ],
  "tokenizer": {
    "type": "char",
    "vocabFile": "tokenizer/vocab.json",
    "sha256": "%4",
    "bosTokenId": 1, "eosTokenId": 2, "padTokenId": 0, "unkTokenId": 3
  },
  "input": {"width": 224, "height": 224, "layout": "NCHW", "color": "RGB",
            "sourceConversion": "RGB", "resizeMode": "stretch", "resample": "bicubic"},
  "normalization": {"scale": 0.00392156862745098, "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225]},
  "generation": {"maxLength": 128, "strategy": "greedy", "repetitionPenalty": 1.2, "contentRunCap": 12},
  "postprocess": "baberu-v1"
})")
                                 .arg(visHash, preHash, stepHash, vocHash);

    writeFakeFile(d.filePath(QStringLiteral("manifest.json")), manifest.toUtf8());
    return dir.path();
}

} // namespace

// ---------------------------------------------------------------------------
// Test case implementations
// ---------------------------------------------------------------------------

void BaberuOcrTest::initTestCase() {}
void BaberuOcrTest::cleanupTestCase() {}

// --- Manifest validation ---

void BaberuOcrTest::manifestRejectsMissingDirectory() {
    auto result = loadAndValidateManifest(QStringLiteral("/nonexistent/path/xyz987"));
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.missing"));
}

void BaberuOcrTest::manifestRejectsMissingManifestFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto result = loadAndValidateManifest(dir.path());
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.missing"));
}

void BaberuOcrTest::manifestRejectsInvalidJson() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFakeFile(QDir(dir.path()).filePath(QStringLiteral("manifest.json")), QByteArray("not json {{{"));
    auto result = loadAndValidateManifest(dir.path());
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void BaberuOcrTest::manifestRejectsUnknownTopLevelField() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    // Re-write manifest with an extra unknown field
    QFile f(QDir(modelDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray existing = f.readAll();
    f.close();
    // Inject unknown field before closing brace
    existing.chop(1); // remove last '}'
    existing.append(R"(, "unknownField": "oops"})");
    writeFakeFile(QDir(modelDir).filePath(QStringLiteral("manifest.json")), existing);
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void BaberuOcrTest::manifestRejectsZeroOpset() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    QFile f(QDir(modelDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    f.close();
    data.replace("\"opset\": 17", "\"opset\": 0");
    writeFakeFile(QDir(modelDir).filePath(QStringLiteral("manifest.json")), data);
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void BaberuOcrTest::manifestRejectsPathTraversalInGraphFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    QFile f(QDir(modelDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    f.close();
    data.replace("vision.onnx", "../vision.onnx");
    writeFakeFile(QDir(modelDir).filePath(QStringLiteral("manifest.json")), data);
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void BaberuOcrTest::manifestRejectsAbsolutePathInGraphFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    QFile f(QDir(modelDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    f.close();
    data.replace("vision.onnx", "/absolute/path.onnx");
    writeFakeFile(QDir(modelDir).filePath(QStringLiteral("manifest.json")), data);
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void BaberuOcrTest::manifestRejectsHashMismatch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    // Tamper with a model file
    writeFakeFile(QDir(modelDir).filePath(QStringLiteral("vision.onnx")), QByteArray(64, 'X'));
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.hash_mismatch"));
}

void BaberuOcrTest::manifestRejectsMissingFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString modelDir = buildValidManifestDir(dir);
    // Remove a model file
    QFile::remove(QDir(modelDir).filePath(QStringLiteral("step.onnx")));
    auto result = loadAndValidateManifest(modelDir);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.missing"));
}

// --- Vocab ---

void BaberuOcrTest::vocabDecodesIdsCorrectly() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray vocabJson = R"(["\u3042","\u3044","\u3046"])"; // a i u in hiragana
    QString vocabPath = QDir(dir.path()).filePath(QStringLiteral("vocab.json"));
    writeFakeFile(vocabPath, vocabJson);

    auto result = BaberuVocab::load(vocabPath, 1, 2, 0, 3);
    QVERIFY(result.has_value());

    // id 4 = chars[0] = hiragana A
    QVector<int> ids = {1, 4, 5, 6, 2}; // bos, a, i, u, eos
    QString decoded = result->decode(ids);
    QCOMPARE(decoded, QString::fromUtf8("\xe3\x81\x82\xe3\x81\x84\xe3\x81\x86")); // あいう
}

void BaberuOcrTest::vocabSkipsSpecialTokens() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray vocabJson = R"(["X"])";
    QString vocabPath = QDir(dir.path()).filePath(QStringLiteral("vocab.json"));
    writeFakeFile(vocabPath, vocabJson);
    auto result = BaberuVocab::load(vocabPath, 1, 2, 0, 3);
    QVERIFY(result.has_value());
    // IDs 0,1,2,3 are special; id 4 is 'X'
    QVector<int> ids = {0, 1, 2, 3, 4};
    QCOMPARE(result->decode(ids), QStringLiteral("X"));
}

void BaberuOcrTest::vocabContentIdsExcludeWaveChars() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Include: 'A' (letter), '5' (digit), hiragana あ.
    // Exclude: 'ー' (wave), '~' (wave), '〜' (wave), 'ｰ' (half-width dash).
    const QByteArray vocabJson = R"(["A","5","\u3042","\u30fc","\uff70","\u301c","~"])";
    QString vocabPath = QDir(dir.path()).filePath(QStringLiteral("vocab.json"));
    writeFakeFile(vocabPath, vocabJson);
    auto result = BaberuVocab::load(vocabPath, 1, 2, 0, 3);
    QVERIFY(result.has_value());
    const auto& cids = result->contentIds();
    QVERIFY(cids.contains(4));   // 'A'
    QVERIFY(cids.contains(5));   // '5'
    QVERIFY(cids.contains(6));   // 'あ'
    QVERIFY(!cids.contains(7));  // 'ー'
    QVERIFY(!cids.contains(8));  // 'ｰ'
    QVERIFY(!cids.contains(9));  // '〜'
    QVERIFY(!cids.contains(10)); // '~'
}

// --- Preprocessor ---

namespace {

// Create an OcrRequest with a full-image rect.
application::OcrRequest makeRequest(const QImage& img) {
    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    req.rect = domain::RectGeometry{0.0, 0.0, static_cast<double>(img.width()), static_cast<double>(img.height())};
    return req;
}

// Build a minimal OcrModelManifest for preprocessor tests.
OcrModelManifest makeManifestForPreprocess() {
    OcrModelManifest m;
    m.input.width = 224;
    m.input.height = 224;
    m.input.sourceConversion = QStringLiteral("RGB");
    m.input.resizeMode = QStringLiteral("stretch");
    m.input.resample = QStringLiteral("bicubic");
    m.normalization.scale = 1.0f / 255.0f;
    m.normalization.mean = {0.485f, 0.456f, 0.406f};
    m.normalization.std = {0.229f, 0.224f, 0.225f};
    return m;
}

constexpr float kTol = 1e-6f;

} // namespace

void BaberuOcrTest::preprocessorOutputsCorrectNCHWShape() {
    QImage img(10, 8, QImage::Format_RGB888);
    img.fill(Qt::red);
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(makeRequest(img), m);
    QVERIFY(result.has_value());
    // Shape: [1, 3, 224, 224] = 150528 floats
    QCOMPARE(static_cast<int>(result->size()), 1 * 3 * 224 * 224);
}

void BaberuOcrTest::preprocessorGoldenSolidRed() {
    // Load fixture: solid_red_2x2.png -> all pixels (255,0,0,255)
    QString path = QDir(fixtureDir()).filePath(QStringLiteral("solid_red_2x2.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found; skipping golden test");
    }
    QImage img(path);
    QVERIFY(!img.isNull());
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(makeRequest(img), m);
    QVERIFY(result.has_value());

    // PIL golden: R=2.248908, G=-2.035714, B=-1.804444 (all pixels same)
    const float goldenR = 2.248908f;
    const float goldenG = -2.035714f;
    const float goldenB = -1.804444f;
    const auto& t = *result;
    const int H = 224, W = 224;
    // Check pixel (0,0) and (112,112)
    for (int pix : {0, 112 * W + 112}) {
        QVERIFY(std::abs(t[0 * H * W + pix] - goldenR) <= kTol);
        QVERIFY(std::abs(t[1 * H * W + pix] - goldenG) <= kTol);
        QVERIFY(std::abs(t[2 * H * W + pix] - goldenB) <= kTol);
    }
}

void BaberuOcrTest::preprocessorGoldenSolidGreen() {
    QString path = QDir(fixtureDir()).filePath(QStringLiteral("solid_green_10x5.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found; skipping golden test");
    }
    QImage img(path);
    QVERIFY(!img.isNull());
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(makeRequest(img), m);
    QVERIFY(result.has_value());

    const float goldenR = -2.117904f;
    const float goldenG = 2.428571f;
    const float goldenB = -1.804444f;
    const auto& t = *result;
    const int H = 224, W = 224;
    for (int pix : {0, 100 * W + 100}) {
        QVERIFY(std::abs(t[0 * H * W + pix] - goldenR) <= kTol);
        QVERIFY(std::abs(t[1 * H * W + pix] - goldenG) <= kTol);
        QVERIFY(std::abs(t[2 * H * W + pix] - goldenB) <= kTol);
    }
}

void BaberuOcrTest::preprocessorGoldenWhite() {
    QImage img(2, 2, QImage::Format_RGB888);
    img.fill(Qt::white);
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(makeRequest(img), m);
    QVERIFY(result.has_value());

    const float goldenR = 2.248908f;
    const float goldenG = 2.428571f;
    const float goldenB = 2.640000f;
    const auto& t = *result;
    const int H = 224, W = 224;
    QVERIFY(std::abs(t[0 * H * W + 0] - goldenR) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 0] - goldenG) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 0] - goldenB) <= kTol);
}

void BaberuOcrTest::preprocessorGoldenGradient() {
    QString path = QDir(fixtureDir()).filePath(QStringLiteral("gradient_4x3.png"));
    if (!QFile::exists(path)) {
        QSKIP("Fixture not found; skipping golden test");
    }
    QImage img(path);
    QVERIFY(!img.isNull());
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(makeRequest(img), m);
    QVERIFY(result.has_value());

    const auto& t = *result;
    const int H = 224, W = 224;
    // Golden at (0,0): R=-2.117904, G=-2.035714, B=0.426493
    QVERIFY(std::abs(t[0 * H * W + 0] - (-2.117904f)) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 0] - (-2.035714f)) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 0] - (0.426493f)) <= kTol);
    // Golden at (112,112): R=-0.559551, G=-0.530112, B=0.426493
    QVERIFY(std::abs(t[0 * H * W + 112 * W + 112] - (-0.559551f)) <= kTol);
    QVERIFY(std::abs(t[1 * H * W + 112 * W + 112] - (-0.530112f)) <= kTol);
    QVERIFY(std::abs(t[2 * H * W + 112 * W + 112] - (0.426493f)) <= kTol);
}

void BaberuOcrTest::preprocessorRejectsEmptyIntersection() {
    QImage img(10, 10, QImage::Format_RGB888);
    img.fill(Qt::blue);
    application::OcrRequest req;
    req.pageId = domain::PageId::generate();
    req.regionId = domain::RegionId::generate();
    req.image = img;
    // Rect outside image bounds -> empty intersection
    req.rect = domain::RectGeometry{20.0, 20.0, 5.0, 5.0};
    auto m = makeManifestForPreprocess();
    auto result = BaberuOcrPreprocessor::preprocess(req, m);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_ocr_region"));
}

// --- Decode logic ---
// These tests exercise the static decode logic directly by calling helpers.
// We test the penalty / cap formulas using simple arithmetic.

void BaberuOcrTest::decoderUsesFakeSessionToVerifyDecoding() {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Test");
    manga::services::BoundedTaskExecutor executor;
    QString manifestDir = QDir(fixtureDir()).absoluteFilePath(QStringLiteral("../manifest"));
    manga::services::OnnxMangaOcrEngine engine(&env, &executor, manifestDir);

    manga::application::OcrRequest req;
    req.regionId = manga::domain::RegionId::generate();
    // Generate a valid image to pass preprocess
    QImage img(224, 224, QImage::Format_RGB888);
    img.fill(Qt::white);
    req.image = img;
    req.rect = manga::domain::RectGeometry(0, 0, 224, 224);

    manga::application::OperationContext ctx;
    auto future = engine.recognize(req, ctx);
    future.waitForFinished();

    auto res = future.result();
    if (!res.has_value()) {
        QFAIL(qPrintable(res.error().safeMessage));
    }

    // In our fake session:
    // prefill outputs 10.
    // 10 -> 11 (logit 5.0) vs 12 (logit 4.0) -> picks 11.
    // 11 (RP=1.2) -> 11 (5.0/1.2=4.16) vs 12 (4.0) -> picks 11.
    // 11 (run cap=2) -> logit -inf -> picks 12.
    // 12 -> 2 (EOS).
    // So sequence is 10, 11, 11, 12.
    // vocab is: id 4=a, 5=b... 10=g, 11=h, 12=i.
    // So output string should be "ghhi".
    QCOMPARE(res.value().text, QStringLiteral("ghhi"));
}

void BaberuOcrTest::decoderMaxLengthReturnsDecodeLimit() {
    // Need to modify manifest to have maxLength = 2
    // But since it's hard to modify on disk safely during parallel tests,
    // we can just copy the manifest dir to a temporary dir, modify it, and test.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString srcDirPath = QDir(fixtureDir()).absoluteFilePath(QStringLiteral("../manifest"));
    QDir srcDir(srcDirPath);
    QDir dstDir(tempDir.path());
    for (const QString& file : srcDir.entryList(QDir::Files)) {
        QFile::copy(srcDir.filePath(file), dstDir.filePath(file));
    }

    // Modify manifest.json
    QFile manifestFile(dstDir.filePath("manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadWrite));
    QByteArray data = manifestFile.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonObject gen = root["generation"].toObject();
    gen["maxLength"] = 2;
    root["generation"] = gen;
    manifestFile.resize(0);
    manifestFile.write(QJsonDocument(root).toJson());
    manifestFile.close();

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Test");
    manga::services::BoundedTaskExecutor executor;
    manga::services::OnnxMangaOcrEngine engine(&env, &executor, dstDir.path());

    manga::application::OcrRequest req;
    req.regionId = manga::domain::RegionId::generate();
    QImage img(224, 224, QImage::Format_RGB888);
    img.fill(Qt::white);
    req.image = img;
    req.rect = manga::domain::RectGeometry(0, 0, 224, 224);

    manga::application::OperationContext ctx;
    auto future = engine.recognize(req, ctx);
    future.waitForFinished();

    auto res = future.result();
    QVERIFY(!res.has_value());
    if (!res.has_value()) {
        QCOMPARE(res.error().code, QStringLiteral("inference.decode_limit"));
    }
}
} // namespace manga::services::test
