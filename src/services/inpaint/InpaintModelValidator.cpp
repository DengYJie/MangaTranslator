#include "InpaintModelValidator.h"

#include "InpaintManifest.h"

namespace manga::services {
application::Result<application::InpaintModelStatus> InpaintModelValidator::validate(const QString& modelDirectory) {
    const auto manifestResult = loadAndValidateManifest(modelDirectory);
    if (!manifestResult)
        return std::unexpected(manifestResult.error());
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
    return status;
}
} // namespace manga::services
