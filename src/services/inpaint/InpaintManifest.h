#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::services {

struct InpaintManifest {
    int manifestVersion{1};
    QString id;
    QString version;
    QString modelDirectory;
    QString modelFile;
    QString inputName;
    QString maskInputName;
    QString outputName;
    QString inputLayout;
    QString outputLayout;
    QString inputColor;
    QString outputColor;
    float inputRangeMin{0.0f};
    float inputRangeMax{1.0f};
    bool dynamicSpatial{false};
    int padMultiple{8};
    int tileCore{512};
    int tileContext{128};
    QString minimumOnnxRuntime;
    QString executionProvider;
};

[[nodiscard]] application::Result<InpaintManifest> loadAndValidateManifest(const QString& modelDirectory);

} // namespace manga::services
