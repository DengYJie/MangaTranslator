#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::application {

/// Identity and validation status of a comic detection/layout analysis model bundle.
struct DetectionModelStatus {
    bool isValid{false};
    QString id;
    QString version;
    QString modelDirectory;
    QString repository;
    QString revision;
    QString license;
    int opset{0};
    QString minimumOnnxRuntime;
    QString executionProvider;

    friend bool operator==(const DetectionModelStatus&, const DetectionModelStatus&) = default;
};

} // namespace manga::application
