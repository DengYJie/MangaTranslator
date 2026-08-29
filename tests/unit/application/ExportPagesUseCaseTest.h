#pragma once
#include <QObject>

class ExportPagesUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulExportForwardsFilteredRequest();
    void testSelectedScopeFiltersSnapshots();
    void testCurrentPageScopeRequiresCurrentPageId();
    void testCurrentPageScopeExportsOnlyCurrentPage();
    void testAllPagesScopeRequiresAllSnapshots();
    void testMissingSnapshotForSelectedPage();
    void testInvalidOutputStemRejected();
    void testInvalidLayerRejected();
    void testProjectMismatchRejected();
    void testQualityOutOfRangeRejected();
    void testDuplicateSnapshotRejected();
    void testCancelledBeforeStart();
    void testInvalidOptionsRejected();
    void testExporterFailurePropagates();
    void testCancellation();
};
