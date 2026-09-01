#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::services {

/// Parsed metadata from koharu-layout-rfdetr-onnx manifest.json
struct KoharuLayoutManifest {
    int manifestVersion{1};
    QString id;
    QString version;
    QString modelDirectory;
    QString modelFile;
    QString repository;
    QString revision;
    QString license;
    int opset{17};
    QString minimumOnnxRuntime;
    QString executionProvider;
    int inputWidth{1152};
    int inputHeight{1152};
    QString inputLayout;
    QString inputColor;
    QString inputName;
    QString detsOutputName;
    QString labelsOutputName;
    QString masksOutputName;
};

[[nodiscard]] application::Result<KoharuLayoutManifest> loadAndValidateDetectionManifest(const QString& modelDirectory);

} // namespace manga::services
