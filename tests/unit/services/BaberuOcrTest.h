#pragma once

#include <QObject>

namespace manga::services::test {

/// Unit tests for BaberuManifest, BaberuVocab, BaberuOcrPreprocessor, and
/// the decode logic. These tests do NOT require a real ONNX model.
class BaberuOcrTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Manifest validation
    void manifestRejectsMissingDirectory();
    void manifestRejectsMissingManifestFile();
    void manifestRejectsInvalidJson();
    void manifestRejectsUnknownTopLevelField();
    void manifestRejectsZeroOpset();
    void manifestRejectsPathTraversalInGraphFile();
    void manifestRejectsAbsolutePathInGraphFile();
    void manifestRejectsHashMismatch();
    void manifestRejectsMissingFile();

    // Vocab
    void vocabDecodesIdsCorrectly();
    void vocabSkipsSpecialTokens();
    void vocabContentIdsExcludeWaveChars();

    // Preprocessor
    void preprocessorOutputsCorrectNCHWShape();
    void preprocessorGoldenSolidRed();
    void preprocessorGoldenSolidGreen();
    void preprocessorGoldenWhite();
    void preprocessorGoldenGradient();
    void preprocessorRejectsEmptyIntersection();

    // Decode logic (fake session / deterministic logits)
    void decoderUsesFakeSessionToVerifyDecoding();
    void decoderMaxLengthReturnsDecodeLimit();
};

} // namespace manga::services::test
