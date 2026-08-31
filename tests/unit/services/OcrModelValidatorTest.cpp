#include "OcrModelValidatorTest.h"

#include "services/ocr/OcrModelValidator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

using namespace manga::application;
using namespace manga::services;

namespace {

bool writeTestFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(data);
    return true;
}

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString createValidModelDir(QTemporaryDir& dir) {
    QDir d(dir.path());
    d.mkdir(QStringLiteral("tokenizer"));

    const QByteArray visData = QByteArray(64, 'V');
    const QByteArray preData = QByteArray(64, 'P');
    const QByteArray stepData = QByteArray(64, 'S');
    writeTestFile(d.filePath(QStringLiteral("vision.onnx")), visData);
    writeTestFile(d.filePath(QStringLiteral("prefill.onnx")), preData);
    writeTestFile(d.filePath(QStringLiteral("step.onnx")), stepData);

    const QByteArray vocabData = QByteArray(R"(["a","b"])");
    writeTestFile(d.filePath(QStringLiteral("tokenizer/vocab.json")), vocabData);

    const QString visHash = sha256Hex(visData);
    const QString preHash = sha256Hex(preData);
    const QString stepHash = sha256Hex(stepData);
    const QString vocHash = sha256Hex(vocabData);

    const QString manifest = QString(R"({
  "manifestVersion": 1,
  "id": "baberu-manga-ocr",
  "version": "1.2.0",
  "source": {
    "repository": "https://github.com/example/baberu-ocr",
    "revision": "d9cc13153e9a1cd8fdfa3b7b1cc329da2020aeae",
    "license": "Apache-2.0"
  },
  "runtime": {
    "minimumOnnxRuntime": "1.20.0",
    "opset": 17,
    "executionProvider": "CPUExecutionProvider"
  },
  "graphs": [
    {"role": "vision", "file": "vision.onnx", "sha256": "%1", "inputs": ["pixel_values"], "outputs": ["vision_embeds"]},
    {"role": "prefill", "file": "prefill.onnx", "sha256": "%2", "inputs": ["vision_embeds", "input_ids"], "outputs": ["logits"]},
    {"role": "step", "file": "step.onnx", "sha256": "%3", "inputs": ["input_ids", "position_ids"], "outputs": ["logits"]}
  ],
  "tokenizer": {
    "type": "char",
    "vocabFile": "tokenizer/vocab.json",
    "sha256": "%4",
    "bosTokenId": 1, "eosTokenId": 2, "padTokenId": 0, "unkTokenId": 3
  },
  "input": {
    "width": 224, "height": 224, "layout": "NCHW", "color": "RGB",
    "sourceConversion": "RGB", "resizeMode": "stretch", "resample": "bicubic"
  },
  "normalization": {"scale": 0.00392156862745098, "mean": [0.485, 0.456, 0.406], "std": [0.229, 0.224, 0.225]},
  "generation": {"maxLength": 128, "strategy": "greedy", "repetitionPenalty": 1.2, "contentRunCap": 12},
  "postprocess": "baberu-v1"
})")
                                 .arg(visHash, preHash, stepHash, vocHash);

    writeTestFile(d.filePath(QStringLiteral("manifest.json")), manifest.toUtf8());
    return dir.path();
}

void rewriteManifest(const QString& modelDir, const std::function<void(QString&)>& edit) {
    QFile f(QDir(modelDir).filePath(QStringLiteral("manifest.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QString json = QString::fromUtf8(f.readAll());
    f.close();
    edit(json);
    writeTestFile(QDir(modelDir).filePath(QStringLiteral("manifest.json")), json.toUtf8());
}

} // namespace

void OcrModelValidatorTest::validatesValidModelDirectory() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString modelDir = createValidModelDir(tempDir);

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(result.has_value());
    const OcrModelStatus& status = *result;
    QVERIFY(status.isValid);
    QCOMPARE(status.id, QStringLiteral("baberu-manga-ocr"));
    QCOMPARE(status.version, QStringLiteral("1.2.0"));
    QCOMPARE(status.modelDirectory, modelDir);
    QCOMPARE(status.repository, QStringLiteral("https://github.com/example/baberu-ocr"));
    QCOMPARE(status.revision, QStringLiteral("d9cc13153e9a1cd8fdfa3b7b1cc329da2020aeae"));
    QCOMPARE(status.license, QStringLiteral("Apache-2.0"));
    QCOMPARE(status.opset, 17);
    QCOMPARE(status.minimumOnnxRuntime, QStringLiteral("1.20.0"));
    QCOMPARE(status.executionProvider, QStringLiteral("CPUExecutionProvider"));
}

void OcrModelValidatorTest::reportsMissingDirectoryWithSafeMessage() {
    const QString missingDir = QDir(QDir::tempPath()).filePath(QStringLiteral("manga-absent-model-bundle"));

    OcrModelValidator validator;
    auto result = validator.validate(missingDir);

    QVERIFY(!result.has_value());
    const ApplicationError& err = result.error();
    QCOMPARE(err.category, ErrorCategory::Model);
    QCOMPARE(err.code, QStringLiteral("model.missing"));
    QCOMPARE(err.recovery, RecoveryAction::LocateModel);
    // The user-visible message names the problem only; the located path stays in the log diagnostic.
    QVERIFY(!err.safeMessage.contains(QFileInfo(missingDir).dir().path()));
    QVERIFY(!err.safeMessage.contains(QFileInfo(missingDir).fileName()));
    QVERIFY(err.diagnostic.contains(missingDir));
}

void OcrModelValidatorTest::reportsMissingManifestJson() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    OcrModelValidator validator;
    auto result = validator.validate(tempDir.path());

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.missing"));
    QCOMPARE(result.error().recovery, RecoveryAction::LocateModel);
}

void OcrModelValidatorTest::reportsInvalidManifestJson() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir d(tempDir.path());
    writeTestFile(d.filePath(QStringLiteral("manifest.json")), QByteArray("{ corrupt json "));

    OcrModelValidator validator;
    auto result = validator.validate(tempDir.path());

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
    QCOMPARE(result.error().recovery, RecoveryAction::LocateModel);
}

void OcrModelValidatorTest::reportsIncompatibleRuntime() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString modelDir = createValidModelDir(tempDir);
    rewriteManifest(modelDir, [](QString& json) {
        json.replace(QStringLiteral("CPUExecutionProvider"), QStringLiteral("InvalidProviderXYZ"));
    });

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void OcrModelValidatorTest::reportsUnsupportedOpset() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString modelDir = createValidModelDir(tempDir);
    rewriteManifest(
        modelDir, [](QString& json) { json.replace(QStringLiteral("\"opset\": 17"), QStringLiteral("\"opset\": 0")); });

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
}

void OcrModelValidatorTest::rejectsBundleWithBlankSourceLicense() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString modelDir = createValidModelDir(tempDir);
    rewriteManifest(modelDir, [](QString& json) {
        json.replace(QStringLiteral("\"license\": \"Apache-2.0\""), QStringLiteral("\"license\": \"  \""));
    });

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.incompatible"));
    QVERIFY(result.error().diagnostic.contains(QStringLiteral("license")));
}

void OcrModelValidatorTest::reportsMissingModelFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString modelDir = createValidModelDir(tempDir);

    QFile::remove(QDir(modelDir).filePath(QStringLiteral("vision.onnx")));

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.missing"));
}

void OcrModelValidatorTest::reportsHashMismatch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString modelDir = createValidModelDir(tempDir);

    // Corrupt one model file
    writeTestFile(QDir(modelDir).filePath(QStringLiteral("vision.onnx")), QByteArray(64, 'Z'));

    OcrModelValidator validator;
    auto result = validator.validate(modelDir);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("model.hash_mismatch"));
}
