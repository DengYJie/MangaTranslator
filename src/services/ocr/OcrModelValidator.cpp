#include "OcrModelValidator.h"

#include "BaberuManifest.h"

namespace manga::services {

application::Result<application::OcrModelStatus> OcrModelValidator::validate(const QString& modelDirectory) {
    // Bundle errors pass through unchanged so callers keep the owning safe message and recovery action.
    auto manifestResult = loadAndValidateManifest(modelDirectory);
    if (!manifestResult.has_value()) {
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

    return status;
}

} // namespace manga::services
