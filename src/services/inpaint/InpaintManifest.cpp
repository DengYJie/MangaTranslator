#include "InpaintManifest.h"

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
bool safeModelFile(const QDir& dir, const QString& file) {
    return !file.isEmpty() && !file.contains(QStringLiteral("..")) && !file.contains(QLatin1Char('/')) &&
           !file.contains(QLatin1Char('\\')) && QFileInfo(dir.filePath(file)).isFile();
}
bool stringArrayEquals(const QJsonValue& value, const QStringList& expected) {
    if (!value.isArray())
        return false;
    const auto array = value.toArray();
    if (array.size() != expected.size())
        return false;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isString() || array.at(i).toString().trimmed() != expected.at(i))
            return false;
    }
    return true;
}
bool versionAtLeast120(const QString& value) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)(?:\\.(\\d+))?$"));
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

    QJsonObject input;
    if (!requireObject(root, QStringLiteral("input"), &input))
        return std::unexpected(incompatible(QStringLiteral("Missing input object")));
    const auto graphs = root.value(QStringLiteral("graphs"));
    const bool graphSchema = !graphs.isUndefined();
    QJsonObject graph;
    if (graphSchema) {
        if (!graphs.isArray() || graphs.toArray().size() != 1 || !graphs.toArray().first().isObject())
            return std::unexpected(incompatible(QStringLiteral("Manifest must contain exactly one graph")));
        graph = graphs.toArray().first().toObject();
        if (stringValue(graph, QStringLiteral("role")) != QLatin1String("inpaint-dynamic"))
            return std::unexpected(incompatible(QStringLiteral("Inpaint graph must be dynamic spatial")));
        result.modelFile = stringValue(graph, QStringLiteral("file"));
        if (!safeModelFile(dir, result.modelFile))
            return std::unexpected(modelError(
                QStringLiteral("model.missing"), QStringLiteral("Inpaint model file not found"), result.modelFile));
        if (!stringArrayEquals(graph.value(QStringLiteral("inputs")),
                               {QStringLiteral("image"), QStringLiteral("mask")}))
            return std::unexpected(incompatible(QStringLiteral("Inpaint graph must expose image and mask inputs")));
        if (!stringArrayEquals(graph.value(QStringLiteral("outputs")), {QStringLiteral("inpainted")}))
            return std::unexpected(incompatible(QStringLiteral("Inpaint graph must expose one inpainted output")));
    } else {
        QJsonObject model;
        if (!requireObject(root, QStringLiteral("model"), &model))
            return std::unexpected(incompatible(QStringLiteral("Missing model object")));
        result.modelFile = stringValue(model, QStringLiteral("file"));
        if (!safeModelFile(dir, result.modelFile))
            return std::unexpected(modelError(
                QStringLiteral("model.missing"), QStringLiteral("Inpaint model file not found"), result.modelFile));
    }
    QJsonObject output;
    const auto outputValue = root.value(QStringLiteral("output"));
    const bool hasOutputObject = outputValue.isObject();
    if (!outputValue.isUndefined() && !hasOutputObject)
        return std::unexpected(incompatible(QStringLiteral("Invalid output object")));
    if (!graphSchema && !hasOutputObject)
        return std::unexpected(incompatible(QStringLiteral("Missing output object")));
    if (hasOutputObject)
        output = root.value(QStringLiteral("output")).toObject();
    result.inputName = graphSchema ? QStringLiteral("image") : stringValue(input, QStringLiteral("name"));
    result.maskInputName = graphSchema ? QStringLiteral("mask") : stringValue(input, QStringLiteral("maskName"));
    result.outputName = graphSchema ? QStringLiteral("inpainted") : stringValue(output, QStringLiteral("name"));
    result.inputLayout = stringValue(input, QStringLiteral("layout"));
    result.outputLayout = stringValue(output, QStringLiteral("layout"));
    result.inputColor = stringValue(input, QStringLiteral("color"));
    result.outputColor = stringValue(output, QStringLiteral("color"));
    if (result.outputLayout.isEmpty())
        result.outputLayout = result.inputLayout;
    if (result.outputColor.isEmpty())
        result.outputColor = result.inputColor;
    result.dynamicSpatial = graphSchema || input.value(QStringLiteral("dynamicSpatial")).toBool(false);
    const auto range = input.value(QStringLiteral("range"));
    if (!range.isUndefined() && (!range.isArray() || range.toArray().size() != 2))
        return std::unexpected(incompatible(QStringLiteral("Input range must contain exactly two numbers")));
    if (range.isArray()) {
        if (!range.toArray().at(0).isDouble() || !range.toArray().at(1).isDouble())
            return std::unexpected(incompatible(QStringLiteral("Input range must contain exactly two numbers")));
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

    const auto tiling = root.value(QStringLiteral("tiling"));
    if (tiling.isUndefined()) {
        if (!graphSchema)
            return std::unexpected(incompatible(QStringLiteral("Missing tiling object")));
        result.padMultiple = 8;
        result.tileCore = 512;
        result.tileContext = 128;
    } else if (tiling.isObject()) {
        const auto object = tiling.toObject();
        result.padMultiple = object.value(QStringLiteral("padMultiple")).toInt(-1);
        result.tileCore = object.value(QStringLiteral("tileCore")).toInt(-1);
        result.tileContext = object.value(QStringLiteral("tileContext")).toInt(-1);
    } else {
        return std::unexpected(incompatible(QStringLiteral("Invalid tiling object")));
    }
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
