#include "OcrModelValidator.h"

#include "BaberuManifest.h"

#include <QDebug>

namespace manga::services {

application::Result<application::OcrModelStatus> OcrModelValidator::validate(const QString& modelDirectory) {
    // Bundle errors pass through unchanged so callers keep the owning safe message and recovery action.
    auto manifestResult = loadAndValidateManifest(modelDirectory);
    if (!manifestResult.has_value()) {
        qWarning().noquote() << "[OcrValidator] invalid model directory=" << modelDirectory
                             << "code=" << manifestResult.error().code
                             << "message=" << manifestResult.error().safeMessage;
        return std::unexpected(manifestResult.error());
    }

    const auto& manifest = *manifestResult;
    application::OcrModelStatus status;
    status.isValid = true;
    status.id = manifest.id;
    status.version = manifest.version;
    status.modelDirectory = manifest.modelDir;
    status.repository = manifest.source.repository;
    status.revision = manifest.source.revision;
    status.license = manifest.source.license;
    status.opset = manifest.runtime.opset;
    status.minimumOnnxRuntime = manifest.runtime.minimumOnnxRuntime;
    status.executionProvider = manifest.runtime.executionProvider;

    qInfo().noquote() << "[OcrValidator] validated successfully dir=" << modelDirectory << "id=" << status.id
                      << "version=" << status.version;

    return status;
}

} // namespace manga::services
