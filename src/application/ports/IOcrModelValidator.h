#pragma once

#include "application/error/ApplicationError.h"

#include <QString>

namespace manga::application {

/// Identity of a validated OCR model bundle. Model directories are user selected, so the recorded
/// path and provenance fields are safe for diagnostics and model status display, never for logs of
/// credential material.
struct OcrModelStatus {
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

    friend bool operator==(const OcrModelStatus&, const OcrModelStatus&) = default;
};

/// Validates that a directory holds a usable OCR model bundle without loading weights.
class IOcrModelValidator {
public:
    virtual ~IOcrModelValidator() = default;

    [[nodiscard]] virtual Result<OcrModelStatus> validate(const QString& modelDirectory) = 0;
};

} // namespace manga::application
