#pragma once

#include <QObject>

#include <memory>

namespace Ort {
struct Env;
}

namespace manga::services {
class BoundedTaskExecutor;
class OnnxMangaOcrEngine;
} // namespace manga::services

namespace manga::services::test {

/// Integration tests that require a real ONNX model.
/// Tests are skipped if MANGA_OCR_MODEL_DIR is not set or the manifest fails validation.
class BaberuOcrIntegrationTest : public QObject {
    Q_OBJECT

public:
    BaberuOcrIntegrationTest();
    ~BaberuOcrIntegrationTest() override;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void integrationManifestLoadsAndValidates();
    void integrationPreprocessorMatchesPilGoldenSolidRed();
    void integrationPreprocessorMatchesPilGoldenGradient();
    void integrationVocabLoadsCorrectly();
    void integrationEngineMeetsCorpusAccuracy();

private:
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<BoundedTaskExecutor> m_executor;
    std::unique_ptr<OnnxMangaOcrEngine> m_engine;
};

} // namespace manga::services::test
