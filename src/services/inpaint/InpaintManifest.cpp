#include "InpaintManifest.h"

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
        QStringLiteral("model.incompatible"), QStringLiteral("Inpaint model is incompatible or corrupt"), detail);
}
application::Result<void> requireObject(const QJsonObject& root, const QString& key, QJsonObject* out) {
    const auto value = root.value(key);
    if (!value.isObject())
        return std::unexpected(incompatible(QStringLiteral("Missing or invalid object: ") + key));
    *out = value.toObject();
    return {};
}
QString stringValue(const QJsonObject& object, const QString& key) {
    return object.value(key).isString() ? object.value(key).toString().trimmed() : QString();
}
bool versionAtLeast120(const QString& value) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const auto match = re.match(value);
    if (!match.hasMatch())
        return false;
    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    return major > 1 || (major == 1 && minor >= 20);
}
} // namespace

application::Result<InpaintManifest> loadAndValidateManifest(const QString& modelDirectory) {
    QDir dir(modelDirectory.trimmed());
    if (!dir.exists())
        return std::unexpected(modelError(
            QStringLiteral("model.missing"), QStringLiteral("Model directory not found"), dir.absolutePath()));
    QFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly))
        return std::unexpected(
            modelError(QStringLiteral("model.missing"), QStringLiteral("manifest.json not found"), dir.absolutePath()));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::unexpected(incompatible(QStringLiteral("Invalid manifest JSON")));

    const auto root = document.object();
    InpaintManifest result;
    result.modelDirectory = dir.absolutePath();
    result.manifestVersion = root.value(QStringLiteral("manifestVersion")).toInt(-1);
    result.id = stringValue(root, QStringLiteral("id"));
    result.version = stringValue(root, QStringLiteral("version"));
    if (result.manifestVersion != 1 || result.id.isEmpty() || result.version.isEmpty())
        return std::unexpected(incompatible(QStringLiteral("Invalid manifest identity")));

    QJsonObject model;
    if (!requireObject(root, QStringLiteral("model"), &model))
        return std::unexpected(incompatible(QStringLiteral("Missing model object")));
    result.modelFile = stringValue(model, QStringLiteral("file"));
    if (result.modelFile.isEmpty() || result.modelFile.contains(QStringLiteral("..")) ||
        result.modelFile.contains(QLatin1Char('/')) || result.modelFile.contains(QLatin1Char('\\')) ||
        !QFileInfo(dir.filePath(result.modelFile)).isFile())
        return std::unexpected(modelError(
            QStringLiteral("model.missing"), QStringLiteral("Inpaint model file not found"), result.modelFile));

    QJsonObject input;
    if (!requireObject(root, QStringLiteral("input"), &input))
        return std::unexpected(incompatible(QStringLiteral("Missing input object")));
    QJsonObject graph;
    const auto graphs = root.value(QStringLiteral("graphs"));
    if (graphs.isArray() && !graphs.toArray().isEmpty() && graphs.toArray().first().isObject())
        graph = graphs.toArray().first().toObject();
    const auto graphInputs = graph.value(QStringLiteral("inputs"));
    const auto graphOutputs = graph.value(QStringLiteral("outputs"));
    QJsonObject output;
    const bool hasOutputObject = requireObject(root, QStringLiteral("output"), &output).has_value();
    result.inputName = stringValue(input, QStringLiteral("name"));
    result.maskInputName = stringValue(input, QStringLiteral("maskName"));
    if (result.inputName.isEmpty() && graphInputs.isArray() && graphInputs.toArray().size() >= 2) {
        result.inputName = graphInputs.toArray().at(0).toString();
        result.maskInputName = graphInputs.toArray().at(1).toString();
    }
    result.outputName = hasOutputObject ? stringValue(output, QStringLiteral("name")) : QString();
    if (result.outputName.isEmpty() && graphOutputs.isArray() && !graphOutputs.toArray().isEmpty())
        result.outputName = graphOutputs.toArray().first().toString();
    result.inputLayout = stringValue(input, QStringLiteral("layout"));
    result.outputLayout = stringValue(output, QStringLiteral("layout"));
    result.inputColor = stringValue(input, QStringLiteral("color"));
    result.outputColor = stringValue(output, QStringLiteral("color"));
    if (result.outputLayout.isEmpty())
        result.outputLayout = result.inputLayout;
    if (result.outputColor.isEmpty())
        result.outputColor = result.inputColor;
    result.dynamicSpatial =
        input.value(QStringLiteral("dynamicSpatial"))
            .toBool(graph.value(QStringLiteral("role")).toString().contains(QStringLiteral("dynamic")));
    const auto range = input.value(QStringLiteral("range"));
    if (range.isArray() && range.toArray().size() == 2) {
        result.inputRangeMin = float(range.toArray().at(0).toDouble(99.0));
        result.inputRangeMax = float(range.toArray().at(1).toDouble(-99.0));
    } else {
        result.inputRangeMin = float(input.value(QStringLiteral("rangeMin")).toDouble(0.0));
        result.inputRangeMax = float(input.value(QStringLiteral("rangeMax")).toDouble(1.0));
    }
    if (input.value(QStringLiteral("maskMode")).toString() != QLatin1String("binary"))
        return std::unexpected(incompatible(QStringLiteral("Inpaint mask must use binary mode")));
    if (result.inputName.isEmpty() || result.maskInputName.isEmpty() || result.outputName.isEmpty() ||
        result.inputLayout != QLatin1String("NCHW") || result.outputLayout != QLatin1String("NCHW") ||
        result.inputColor != QLatin1String("RGB") || result.outputColor != QLatin1String("RGB") ||
        !result.dynamicSpatial || result.inputRangeMin != 0.0f || result.inputRangeMax != 1.0f)
        return std::unexpected(
            incompatible(QStringLiteral("Inpaint model must be dynamic spatial RGB NCHW in range 0..1")));

    QJsonObject tiling;
    if (!requireObject(root, QStringLiteral("tiling"), &tiling))
        return std::unexpected(incompatible(QStringLiteral("Missing tiling object")));
    result.padMultiple = tiling.value(QStringLiteral("padMultiple")).toInt(-1);
    result.tileCore = tiling.value(QStringLiteral("tileCore")).toInt(-1);
    result.tileContext = tiling.value(QStringLiteral("tileContext")).toInt(-1);
    if (result.padMultiple != 8 || result.tileCore != 512 || result.tileContext != 128)
        return std::unexpected(incompatible(QStringLiteral("Unsupported inpaint tiling configuration")));

    QJsonObject runtime;
    if (!requireObject(root, QStringLiteral("runtime"), &runtime))
        return std::unexpected(incompatible(QStringLiteral("Missing runtime object")));
    result.minimumOnnxRuntime = stringValue(runtime, QStringLiteral("minimumOnnxRuntime"));
    result.executionProvider = stringValue(runtime, QStringLiteral("executionProvider"));
    if (!versionAtLeast120(result.minimumOnnxRuntime) ||
        (result.executionProvider != QLatin1String("cpu") &&
         result.executionProvider != QLatin1String("CPUExecutionProvider")))
        return std::unexpected(incompatible(QStringLiteral("ONNX Runtime 1.20 CPU is required")));
    return result;
}
} // namespace manga::services
