#pragma once

#include <QObject>

class ComicProjectTest : public QObject {
    Q_OBJECT

private slots:
    void testProjectWideRegionIdUniqueness();
    void testBatchEditSuccess();
    void testBatchEditRollbackRestoresRevisionAndDirty();
    void testEmptyBatchDoesNotMarkDirty();
    void testMarkCleanRejectsStaleSaveRevision();
    void testApplyClearChangeOnPageSuccess();
    void testApplyClearChangeOnPageRejectsUnknownIds();
};
