#include "KoharuLayoutManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace manga::services {

namespace {

application::ApplicationError modelError(const QString& code, const QString& safe, const QString& detail) {
    auto error = application::makeApplicationError(
        application::ErrorCategory::Model, code, safe, application::RecoveryAction::LocateModel);
    error.diagnostic = detail;
    return error;
}

application::ApplicationError incompatible(const QString& detail) {
    return modelError(
        QStringLiteral("model.incompatible"), QStringLiteral("Detection model is incompatible or corrupt"), detail);
}

application::Result<void> requireObject(const QJsonObject& root, const QString& key, QJsonObject* out) {
    const auto value = root.value(key);
    if (!value.isObject()) {
        return std::unexpected(incompatible(QStringLiteral("Missing or invalid object: ") + key));
    }
    *out = value.toObject();
    return {};
}

QString stringValue(const QJsonObject& object, const QString& key) {
    return object.value(key).isString() ? object.value(key).toString().trimmed() : QString();
}

bool versionAtLeast120(const QString& value) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const auto match = re.match(value);
    if (!match.hasMatch()) {
        return false;
    }
    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    return major > 1 || (major == 1 && minor >= 20);
}

} // namespace

application::Result<KoharuLayoutManifest> loadAndValidateDetectionManifest(const QString& modelDirectory) {
    QDir dir(modelDirectory.trimmed());
    if (!dir.exists()) {
        return std::unexpected(modelError(
            QStringLiteral("model.missing"), QStringLiteral("Model directory not found"), dir.absolutePath()));
    }

    QFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            modelError(QStringLiteral("model.missing"), QStringLiteral("manifest.json not found"), dir.absolutePath()));
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(incompatible(QStringLiteral("Invalid manifest JSON")));
    }

    const auto root = document.object();
    KoharuLayoutManifest result;
    result.modelDirectory = dir.absolutePath();
    result.manifestVersion = root.value(QStringLiteral("manifestVersion")).toInt(-1);
    result.id = stringValue(root, QStringLiteral("id"));
    result.version = stringValue(root, QStringLiteral("version"));

    if (result.manifestVersion != 1 || result.id.isEmpty() || result.version.isEmpty()) {
        return std::unexpected(incompatible(QStringLiteral("Invalid manifest identity")));
    }

    // Source provenance
    QJsonObject source;
    if (requireObject(root, QStringLiteral("source"), &source).has_value()) {
        result.repository = stringValue(source, QStringLiteral("repository"));
        result.revision = stringValue(source, QStringLiteral("revision"));
        result.license = stringValue(source, QStringLiteral("license"));
    }

    // Runtime
    QJsonObject runtime;
    if (!requireObject(root, QStringLiteral("runtime"), &runtime)) {
        return std::unexpected(incompatible(QStringLiteral("Missing runtime object")));
    }
    result.minimumOnnxRuntime = stringValue(runtime, QStringLiteral("minimumOnnxRuntime"));
    result.opset = runtime.value(QStringLiteral("opset")).toInt(result.opset);
    result.executionProvider = stringValue(runtime, QStringLiteral("executionProvider"));

    if (!versionAtLeast120(result.minimumOnnxRuntime)) {
        return std::unexpected(incompatible(QStringLiteral("ONNX Runtime >= 1.20 is required")));
    }

    // Graphs
    const auto graphsValue = root.value(QStringLiteral("graphs"));
    if (!graphsValue.isArray() || graphsValue.toArray().isEmpty()) {
        return std::unexpected(incompatible(QStringLiteral("Missing graphs array")));
    }

    const auto graphObj = graphsValue.toArray().first().toObject();
    result.modelFile = stringValue(graphObj, QStringLiteral("file"));
    if (result.modelFile.isEmpty() || result.modelFile.contains(QStringLiteral("..")) ||
        result.modelFile.contains(QLatin1Char('/')) || result.modelFile.contains(QLatin1Char('\\')) ||
        !QFileInfo(dir.filePath(result.modelFile)).isFile()) {
        return std::unexpected(modelError(
            QStringLiteral("model.missing"), QStringLiteral("Detection model ONNX file not found"), result.modelFile));
    }

    const auto graphInputs = graphObj.value(QStringLiteral("inputs")).toArray();
    if (!graphInputs.isEmpty()) {
        result.inputName = graphInputs.first().toString();
    }
    if (result.inputName.isEmpty()) {
        result.inputName = QStringLiteral("input");
    }

    const auto graphOutputs = graphObj.value(QStringLiteral("outputs")).toArray();
    if (graphOutputs.size() >= 3) {
        result.detsOutputName = graphOutputs.at(0).toString();
        result.labelsOutputName = graphOutputs.at(1).toString();
        result.masksOutputName = graphOutputs.at(2).toString();
    } else {
        result.detsOutputName = QStringLiteral("dets");
        result.labelsOutputName = QStringLiteral("labels");
        result.masksOutputName = QStringLiteral("masks");
    }

    // Input geometry and format
    QJsonObject inputObj;
    if (!requireObject(root, QStringLiteral("input"), &inputObj)) {
        return std::unexpected(incompatible(QStringLiteral("Missing input object")));
    }

    result.inputWidth = inputObj.value(QStringLiteral("width")).toInt(1152);
    result.inputHeight = inputObj.value(QStringLiteral("height")).toInt(1152);
    result.inputLayout = stringValue(inputObj, QStringLiteral("layout"));
    result.inputColor = stringValue(inputObj, QStringLiteral("color"));

    if (result.inputWidth <= 0 || result.inputHeight <= 0 || result.inputLayout != QLatin1String("NCHW") ||
        result.inputColor != QLatin1String("RGB")) {
        return std::unexpected(
            incompatible(QStringLiteral("Detection model requires 1152x1152 RGB NCHW input specifications")));
    }

    return result;
}

} // namespace manga::services
