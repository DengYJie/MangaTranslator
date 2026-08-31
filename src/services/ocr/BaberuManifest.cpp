#include "BaberuManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace manga::services {

namespace {

static bool isSafeRelativePath(const QString& path) {
    if (path.contains(QStringLiteral("..")) || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))) {
        return false;
    }
    return true;
}

static application::ApplicationError
makeModelError(const QString& code, const QString& safeMessage, const QString& diagnostic) {
    auto error = application::makeApplicationError(
        application::ErrorCategory::Model, code, safeMessage, application::RecoveryAction::LocateModel);
    // The directory path and backend detail stay in the diagnostic; only safeMessage may reach the screen.
    error.diagnostic = diagnostic;
    return error;
}

static application::ApplicationError makeIncompatibleError(const QString& detail) {
    return makeModelError(
        QStringLiteral("model.incompatible"), QStringLiteral("OCR model is incompatible or corrupt"), detail);
}

static application::Result<void>
checkUnknownFields(const QJsonObject& obj, const QStringList& allowed, const QString& objectName) {
    for (const QString& key : obj.keys()) {
        if (!allowed.contains(key)) {
            return std::unexpected<application::ApplicationError>(
                makeIncompatibleError(QStringLiteral("Unknown field in ") + objectName + QStringLiteral(": ") + key));
        }
    }
    return {};
}

static application::Result<void> checkEnum(const QString& value, const QStringList& allowed, const QString& fieldName) {
    if (!allowed.contains(value)) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Invalid value for ") + fieldName + QStringLiteral(": ") + value));
    }
    return {};
}

static application::Result<void> checkSha256(const QString& sha256, const QString& fieldName) {
    static const QRegularExpression regex(QStringLiteral("^[a-f0-9]{64}$"));
    if (!regex.match(sha256).hasMatch()) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Invalid sha256 for ") + fieldName));
    }
    return {};
}

/// Reads a required text member. Blank or wrongly typed values fail closed so retained identity
/// fields always describe the bundle that was validated.
static application::Result<QString> requireStringField(const QJsonObject& obj, const QString& key) {
    const QJsonValue value = obj.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Missing or invalid field: ") + key));
    }
    return value.toString();
}

} // namespace

application::Result<OcrModelManifest> loadAndValidateManifest(const QString& modelDir) {
    QDir dir(modelDir);
    if (!dir.exists()) {
        return std::unexpected<application::ApplicationError>(
            makeModelError(QStringLiteral("model.missing"),
                           QStringLiteral("Model directory not found"),
                           QStringLiteral("Directory does not exist: ") + modelDir));
    }

    QFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return std::unexpected<application::ApplicationError>(
            makeModelError(QStringLiteral("model.missing"),
                           QStringLiteral("manifest.json not found"),
                           QStringLiteral("manifest.json not found in ") + modelDir));
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Invalid manifest JSON")));
    }

    QJsonObject obj = doc.object();
    OcrModelManifest m;
    m.modelDir = modelDir;

    const QStringList knownTopLevel = {QStringLiteral("manifestVersion"),
                                       QStringLiteral("id"),
                                       QStringLiteral("version"),
                                       QStringLiteral("source"),
                                       QStringLiteral("runtime"),
                                       QStringLiteral("graphs"),
                                       QStringLiteral("tokenizer"),
                                       QStringLiteral("input"),
                                       QStringLiteral("normalization"),
                                       QStringLiteral("generation"),
                                       QStringLiteral("postprocess")};
    if (auto res = checkUnknownFields(obj, knownTopLevel, QStringLiteral("root")); !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    for (const auto& key : knownTopLevel) {
        if (!obj.contains(key)) {
            return std::unexpected<application::ApplicationError>(
                makeIncompatibleError(QStringLiteral("Missing required field: ") + key));
        }
    }

    m.manifestVersion = obj.value(QStringLiteral("manifestVersion")).toInt(-1);
    if (m.manifestVersion != 1) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Unsupported manifestVersion")));
    }

    m.id = obj.value(QStringLiteral("id")).toString();
    m.version = obj.value(QStringLiteral("version")).toString();

    QJsonObject sourceObj = obj.value(QStringLiteral("source")).toObject();
    if (auto res =
            checkUnknownFields(sourceObj,
                               {QStringLiteral("repository"), QStringLiteral("revision"), QStringLiteral("license")},
                               QStringLiteral("source"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    auto repository = requireStringField(sourceObj, QStringLiteral("repository"));
    if (!repository.has_value())
        return std::unexpected<application::ApplicationError>(repository.error());
    auto revision = requireStringField(sourceObj, QStringLiteral("revision"));
    if (!revision.has_value())
        return std::unexpected<application::ApplicationError>(revision.error());
    auto license = requireStringField(sourceObj, QStringLiteral("license"));
    if (!license.has_value())
        return std::unexpected<application::ApplicationError>(license.error());
    m.source.repository = *repository;
    m.source.revision = *revision;
    m.source.license = *license;

    QJsonObject runtime = obj.value(QStringLiteral("runtime")).toObject();
    if (auto res = checkUnknownFields(
            runtime,
            {QStringLiteral("opset"), QStringLiteral("minimumOnnxRuntime"), QStringLiteral("executionProvider")},
            QStringLiteral("runtime"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    int opset = runtime.value(QStringLiteral("opset")).toInt(0);
    if (opset < 1) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Invalid or missing opset")));
    }
    auto minimumOnnxRuntime = requireStringField(runtime, QStringLiteral("minimumOnnxRuntime"));
    if (!minimumOnnxRuntime.has_value())
        return std::unexpected<application::ApplicationError>(minimumOnnxRuntime.error());
    if (auto res = checkEnum(runtime.value(QStringLiteral("executionProvider")).toString(),
                             {QStringLiteral("cpu"),
                              QStringLiteral("CPUExecutionProvider"),
                              QStringLiteral("CoreMLExecutionProvider"),
                              QStringLiteral("CUDAExecutionProvider"),
                              QStringLiteral("DmlExecutionProvider")},
                             QStringLiteral("runtime.executionProvider"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.runtime.opset = opset;
    m.runtime.minimumOnnxRuntime = *minimumOnnxRuntime;
    m.runtime.executionProvider = runtime.value(QStringLiteral("executionProvider")).toString();

    QJsonArray graphsArray = obj.value(QStringLiteral("graphs")).toArray();
    if (graphsArray.size() != 3) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Must have exactly 3 graphs")));
    }

    bool hasVision = false, hasPrefill = false, hasStep = false;

    for (const QJsonValue& val : graphsArray) {
        QJsonObject gObj = val.toObject();
        if (auto res = checkUnknownFields(gObj,
                                          {QStringLiteral("role"),
                                           QStringLiteral("file"),
                                           QStringLiteral("sha256"),
                                           QStringLiteral("inputs"),
                                           QStringLiteral("outputs")},
                                          QStringLiteral("graphs item"));
            !res.has_value())
            return std::unexpected<application::ApplicationError>(res.error());

        OcrGraphManifest gm;
        gm.role = gObj.value(QStringLiteral("role")).toString();
        gm.file = gObj.value(QStringLiteral("file")).toString();
        gm.sha256 = gObj.value(QStringLiteral("sha256")).toString();
        for (const auto& in : gObj.value(QStringLiteral("inputs")).toArray())
            gm.inputs.append(in.toString());
        for (const auto& out : gObj.value(QStringLiteral("outputs")).toArray())
            gm.outputs.append(out.toString());

        if (auto res = checkEnum(gm.role,
                                 {QStringLiteral("vision"),
                                  QStringLiteral("decoder-prefill"),
                                  QStringLiteral("prefill"),
                                  QStringLiteral("decoder-step"),
                                  QStringLiteral("step")},
                                 QStringLiteral("graphs.role"));
            !res.has_value())
            return std::unexpected<application::ApplicationError>(res.error());
        if (gm.role == QStringLiteral("vision")) {
            if (hasVision)
                return std::unexpected<application::ApplicationError>(
                    makeIncompatibleError(QStringLiteral("Duplicate graph role: vision")));
            hasVision = true;
        }
        if (gm.role == QStringLiteral("decoder-prefill") || gm.role == QStringLiteral("prefill")) {
            if (hasPrefill)
                return std::unexpected<application::ApplicationError>(
                    makeIncompatibleError(QStringLiteral("Duplicate graph role: prefill")));
            hasPrefill = true;
        }
        if (gm.role == QStringLiteral("decoder-step") || gm.role == QStringLiteral("step")) {
            if (hasStep)
                return std::unexpected<application::ApplicationError>(
                    makeIncompatibleError(QStringLiteral("Duplicate graph role: step")));
            hasStep = true;
        }

        if (auto res = checkSha256(gm.sha256, QStringLiteral("graph ") + gm.role); !res.has_value())
            return std::unexpected<application::ApplicationError>(res.error());
        if (!isSafeRelativePath(gm.file)) {
            return std::unexpected<application::ApplicationError>(
                makeIncompatibleError(QStringLiteral("Unsafe path in graph file: ") + gm.file));
        }
        m.graphs.append(gm);
    }
    if (!hasVision || !hasPrefill || !hasStep) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Missing required graph role")));
    }

    QJsonObject tokenizerObj = obj.value(QStringLiteral("tokenizer")).toObject();
    if (auto res = checkUnknownFields(tokenizerObj,
                                      {QStringLiteral("type"),
                                       QStringLiteral("vocabFile"),
                                       QStringLiteral("sha256"),
                                       QStringLiteral("bosTokenId"),
                                       QStringLiteral("eosTokenId"),
                                       QStringLiteral("padTokenId"),
                                       QStringLiteral("unkTokenId")},
                                      QStringLiteral("tokenizer"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.tokenizer.type = tokenizerObj.value(QStringLiteral("type")).toString();
    if (auto res = checkEnum(m.tokenizer.type,
                             {QStringLiteral("baberu-character-v1"),
                              QStringLiteral("char"),
                              QStringLiteral("spm"),
                              QStringLiteral("bpe"),
                              QStringLiteral("wordpiece")},
                             QStringLiteral("tokenizer.type"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.tokenizer.vocabFile = tokenizerObj.value(QStringLiteral("vocabFile")).toString();
    m.tokenizer.sha256 = tokenizerObj.value(QStringLiteral("sha256")).toString();
    m.tokenizer.bosTokenId = tokenizerObj.value(QStringLiteral("bosTokenId")).toInt(1);
    m.tokenizer.eosTokenId = tokenizerObj.value(QStringLiteral("eosTokenId")).toInt(2);
    m.tokenizer.padTokenId = tokenizerObj.value(QStringLiteral("padTokenId")).toInt(0);
    m.tokenizer.unkTokenId = tokenizerObj.value(QStringLiteral("unkTokenId")).toInt(3);

    if (auto res = checkSha256(m.tokenizer.sha256, QStringLiteral("tokenizer")); !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    if (!isSafeRelativePath(m.tokenizer.vocabFile)) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("Unsafe path in vocabFile: ") + m.tokenizer.vocabFile));
    }

    QJsonObject inputObj = obj.value(QStringLiteral("input")).toObject();
    if (auto res = checkUnknownFields(inputObj,
                                      {QStringLiteral("width"),
                                       QStringLiteral("height"),
                                       QStringLiteral("layout"),
                                       QStringLiteral("color"),
                                       QStringLiteral("sourceConversion"),
                                       QStringLiteral("resizeMode"),
                                       QStringLiteral("resample")},
                                      QStringLiteral("input"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.input.width = inputObj.value(QStringLiteral("width")).toInt(224);
    m.input.height = inputObj.value(QStringLiteral("height")).toInt(224);
    m.input.layout = inputObj.value(QStringLiteral("layout")).toString();
    m.input.color = inputObj.value(QStringLiteral("color")).toString();
    m.input.sourceConversion = inputObj.value(QStringLiteral("sourceConversion")).toString();
    m.input.resizeMode = inputObj.value(QStringLiteral("resizeMode")).toString();
    m.input.resample = inputObj.value(QStringLiteral("resample")).toString();

    if (auto res =
            checkEnum(m.input.layout, {QStringLiteral("NCHW"), QStringLiteral("NHWC")}, QStringLiteral("input.layout"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    if (auto res = checkEnum(m.input.color,
                             {QStringLiteral("RGB"), QStringLiteral("BGR"), QStringLiteral("Grayscale")},
                             QStringLiteral("input.color"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    if (auto res = checkEnum(m.input.sourceConversion,
                             {QStringLiteral("RGB"), QStringLiteral("Grayscale")},
                             QStringLiteral("input.sourceConversion"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    if (auto res = checkEnum(m.input.resizeMode,
                             {QStringLiteral("resize-only"), QStringLiteral("stretch"), QStringLiteral("pad")},
                             QStringLiteral("input.resizeMode"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    if (auto res = checkEnum(m.input.resample,
                             {QStringLiteral("nearest"), QStringLiteral("bilinear"), QStringLiteral("bicubic")},
                             QStringLiteral("input.resample"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    QJsonObject normObj = obj.value(QStringLiteral("normalization")).toObject();
    if (auto res = checkUnknownFields(normObj,
                                      {QStringLiteral("scale"), QStringLiteral("mean"), QStringLiteral("std")},
                                      QStringLiteral("normalization"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.normalization.scale = static_cast<float>(normObj.value(QStringLiteral("scale")).toDouble(1.0 / 255.0));
    if (m.normalization.scale == 0.0f) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("normalization scale cannot be 0")));
    }

    QJsonArray meanArr = normObj.value(QStringLiteral("mean")).toArray();
    QJsonArray stdArr = normObj.value(QStringLiteral("std")).toArray();
    for (int i = 0; i < 3 && i < meanArr.size(); ++i) {
        m.normalization.mean[i] = static_cast<float>(meanArr[i].toDouble(0.0));
    }
    for (int i = 0; i < 3 && i < stdArr.size(); ++i) {
        m.normalization.std[i] = static_cast<float>(stdArr[i].toDouble(1.0));
        if (m.normalization.std[i] == 0.0f) {
            return std::unexpected<application::ApplicationError>(
                makeIncompatibleError(QStringLiteral("normalization std cannot be 0")));
        }
    }

    QJsonObject genObj = obj.value(QStringLiteral("generation")).toObject();
    if (auto res = checkUnknownFields(genObj,
                                      {QStringLiteral("maxLength"),
                                       QStringLiteral("strategy"),
                                       QStringLiteral("repetitionPenalty"),
                                       QStringLiteral("contentRunCap")},
                                      QStringLiteral("generation"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());

    m.generation.maxLength = genObj.value(QStringLiteral("maxLength")).toInt(128);
    m.generation.strategy = genObj.value(QStringLiteral("strategy")).toString();
    if (auto res = checkEnum(m.generation.strategy,
                             {QStringLiteral("greedy"), QStringLiteral("beam"), QStringLiteral("nucleus")},
                             QStringLiteral("generation.strategy"));
        !res.has_value())
        return std::unexpected<application::ApplicationError>(res.error());
    m.generation.repetitionPenalty =
        static_cast<float>(genObj.value(QStringLiteral("repetitionPenalty")).toDouble(1.2));
    m.generation.contentRunCap = genObj.value(QStringLiteral("contentRunCap")).toInt(12);

    if (!obj.value(QStringLiteral("postprocess")).isString()) {
        return std::unexpected<application::ApplicationError>(
            makeIncompatibleError(QStringLiteral("postprocess must be string")));
    }
    m.postprocess = obj.value(QStringLiteral("postprocess")).toString();

    struct FileCheck {
        QString relPath;
        QString expectedHash;
    };
    QVector<FileCheck> checks;
    for (const auto& g : m.graphs) {
        checks.append({g.file, g.sha256});
    }
    checks.append({m.tokenizer.vocabFile, m.tokenizer.sha256});

    for (const auto& check : checks) {
        QString absPath = dir.filePath(check.relPath);
        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly)) {
            return std::unexpected<application::ApplicationError>(
                makeModelError(QStringLiteral("model.missing"),
                               QStringLiteral("Required file not found: ") + check.relPath,
                               QStringLiteral("File not found at ") + absPath));
        }
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(&f);
        QString actualHash = QString::fromLatin1(hasher.result().toHex());
        if (actualHash.compare(check.expectedHash, Qt::CaseInsensitive) != 0) {
            return std::unexpected<application::ApplicationError>(makeModelError(
                QStringLiteral("model.hash_mismatch"),
                QStringLiteral("Hash mismatch for: ") + check.relPath,
                QStringLiteral("Expected %1 but got %2 for %3").arg(check.expectedHash, actualHash, absPath)));
        }
    }

    return m;
}

} // namespace manga::services
