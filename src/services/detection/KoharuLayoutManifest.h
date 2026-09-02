#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::services {

/// Parsed metadata from koharu-layout-rfdetr-onnx manifest.json.
struct KoharuLayoutManifest {
    int manifestVersion{1};
    QString id;
    QString version;
    QString modelDirectory;
    QString modelFile;
    QString repository;
    QString revision;
    QString license;
    int opset{17}; // runtime.opset, reported for diagnostics only; no load path rejects on it.
    QString minimumOnnxRuntime;
    QString executionProvider;
    // input.width and input.height in pixels: the edge the page is stretched to and the tensor shape
    // built from it. The literals only describe a struct that has not been parsed yet.
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
