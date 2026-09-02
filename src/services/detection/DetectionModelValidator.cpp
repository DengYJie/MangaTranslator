#include "DetectionModelValidator.h"

#include "services/detection/KoharuLayoutManifest.h"

#include <QDebug>

namespace manga::services {

application::Result<application::DetectionModelStatus>
DetectionModelValidator::validate(const QString& modelDirectory) {
    auto manifestResult = loadAndValidateDetectionManifest(modelDirectory);
    if (!manifestResult.has_value()) {
        qWarning().noquote() << "[DetectValidator] invalid model directory=" << modelDirectory
                             << "code=" << manifestResult.error().code
                             << "message=" << manifestResult.error().safeMessage;
        return std::unexpected(manifestResult.error());
    }

    const auto& manifest = *manifestResult;
    application::DetectionModelStatus status;
    status.isValid = true;
    status.id = manifest.id;
    status.version = manifest.version;
    status.modelDirectory = manifest.modelDirectory;
    status.repository = manifest.repository;
    status.revision = manifest.revision;
    status.license = manifest.license;
    status.opset = manifest.opset;
    status.minimumOnnxRuntime = manifest.minimumOnnxRuntime;
    status.executionProvider = manifest.executionProvider;

    qInfo().noquote() << "[DetectValidator] validated successfully dir=" << modelDirectory << "id=" << status.id
                      << "version=" << status.version;

    return status;
}

} // namespace manga::services
