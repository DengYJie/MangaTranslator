#pragma once

#include <QObject>

class RegionTextUseCasesTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testSuccessfulCreateRegion();
    void testInvalidCreateRegion();
    void testSuccessfulDeleteRegion();
    void testMoveAndResizeRegion();
    void testMissingRegionRejected();
    void testInsertRegionAtExactOrder();
    void testInsertRegionRejectsInvalidInput();
    void testRemoveRegionReturnsFullRegionAndOrder();
    void testRemoveRegionNotFound();
    void testApplyRegionSnapshotsSuccess();
    void testApplyRegionSnapshotsBatchValidationFailureModifiesNothing();
    void testApplyRegionSnapshotsEmptyEditsIsNoop();

    void testSuccessfulSourceTextEdit();
    void testSourceTextEditFailsOnStaleRevision();
    void testSuccessfulTranslationEdit();
    void testTranslationEditFailsOnStaleRevision();
    void testSuccessfulCandidateSelection();
    void testCandidateSelectionFailsOnInvalidCandidate();
    void testCandidateSelectionFailsOnStaleRevision();
};
