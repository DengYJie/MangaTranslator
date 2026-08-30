#pragma once
#include <QObject>

class RegionUseCasesTest : public QObject {
    Q_OBJECT
private slots:
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
};
