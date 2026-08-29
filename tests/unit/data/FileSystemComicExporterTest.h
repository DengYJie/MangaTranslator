#pragma once

#include <QObject>

class FileSystemComicExporterTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void goldenFourLayerSemiTransparentCompositeMatchesAlphaBlendingFormula();
    void jpegExportFlattensOverOpaqueWhiteAndExcludesMetadata();
    void neverOverwriteAllocatesIncrementedFilenamesAndPreservesExistingHashes();
    void candidateExhaustionReturnsNameExhausted();
    void lockedDirectoryReturnsOutputLocked();
    void invalidLayerGeometryOrFormatFailsWithoutAffectingOtherPages();
    void noTemporaryOrLockFilesRemainAfterExport();
    void stopRequestedBeforeStartCancelsExport();
    void stopAfterFirstPageReturnsPartialSuccess();
    void invalidOutputStemIsRejected();
    void jpegQualityRangeAffectsFileSize();
};
