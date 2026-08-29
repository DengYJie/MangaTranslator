#pragma once

#include <QObject>

namespace manga::services::test {

/// Integration tests that require a real ONNX model.
/// Tests are skipped if MANGA_OCR_MODEL_DIR is not set or the manifest fails validation.
class BaberuOcrIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void integrationManifestLoadsAndValidates();
    void integrationPreprocessorMatchesPilGoldenSolidRed();
    void integrationPreprocessorMatchesPilGoldenGradient();
    void integrationVocabLoadsCorrectly();
    void integrationEngineRecognizesText();
};

} // namespace manga::services::test
