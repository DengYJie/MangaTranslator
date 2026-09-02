#pragma once
#include <QObject>

class ImportImagesUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulImportAndSorting();
    void testDuplicatePathsRejected();
    void testEquivalentPathsRejected();
    void testImporterFailurePropagates();
    void testCancellation();
    void testApplyImportResultAddsPages();
    void testApplyImportResultRejectsStaleGeneration();
    void testApplyImportResultRollsBackInvalidBatch();
    void testDiscardImportedAssetsValidatesAndDispatches();
    void testAcknowledgeImportedAssetsValidatesAndDispatches();
};
