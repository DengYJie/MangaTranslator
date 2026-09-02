#pragma once

#include <QObject>

class ComicProjectTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testProjectWideRegionIdUniqueness();
    void testBatchEditSuccess();
    void testBatchEditRollbackRestoresRevisionAndDirty();
    void testEmptyBatchDoesNotMarkDirty();
    void testMarkCleanRejectsStaleSaveRevision();
    void testApplyClearChangeOnPageSuccess();
    void testApplyClearChangeOnPageRejectsUnknownIds();
    void testInsertRegionOnPageSuccessAndRevision();
    void testInsertRegionOnPageDuplicateIdRejected();
    void testInsertRegionOnPageInvalidInputLeavesProjectUnmodified();
};
