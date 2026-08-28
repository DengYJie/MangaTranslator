#pragma once
#include <QObject>

class ImportImagesUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulImportAndSorting();
    void testDuplicatePathsRejected();
    void testEquivalentPathsRejected();
    void testImporterFailurePropagates();
    void testCancellation();
    void testApplyImportResultAddsPages();
    void testApplyImportResultRejectsStaleGeneration();
    void testApplyImportResultRollsBackInvalidBatch();
    void testDiscardImportedAssetsValidatesAndDispatches();
};
