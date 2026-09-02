#include "InpaintModelValidator.h"

#include "InpaintManifest.h"

#include <QDebug>

namespace manga::services {
application::Result<application::InpaintModelStatus> InpaintModelValidator::validate(const QString& modelDirectory) {
    const auto manifestResult = loadAndValidateManifest(modelDirectory);
    if (!manifestResult) {
        qWarning().noquote() << "[InpaintValidator] invalid model directory=" << modelDirectory
                             << "code=" << manifestResult.error().code
                             << "message=" << manifestResult.error().safeMessage;
        return std::unexpected(manifestResult.error());
    }
    const auto& manifest = *manifestResult;
    application::InpaintModelStatus status;
    status.isValid = true;
    status.id = manifest.id;
    status.version = manifest.version;
    status.modelDirectory = manifest.modelDirectory;
    status.modelFile = manifest.modelFile;
    status.inputName = manifest.inputName;
    status.outputName = manifest.outputName;
    status.minimumOnnxRuntime = manifest.minimumOnnxRuntime;
    status.executionProvider = manifest.executionProvider;

    qInfo().noquote() << "[InpaintValidator] validated successfully dir=" << modelDirectory << "id=" << status.id
                      << "version=" << status.version;

    return status;
}
} // namespace manga::services
