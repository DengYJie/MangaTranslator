#pragma once

#include <QObject>

class OcrModelValidatorTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void validatesValidModelDirectory();
    void reportsMissingDirectoryWithSafeMessage();
    void reportsMissingManifestJson();
    void reportsInvalidManifestJson();
    void reportsIncompatibleRuntime();
    void reportsUnsupportedOpset();
    void rejectsBundleWithBlankSourceLicense();
    void reportsMissingModelFile();
    void reportsHashMismatch();
};
