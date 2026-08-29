#pragma once

#include <QObject>

class ApplicationUseCasesTest : public QObject {
    Q_OBJECT

private slots:
    void testReorderRegionChangesReadingOrder();
    void testReorderRegionRejectsInvalidIndex();
    void testExportResolvesSelectedPages();
    void testExportRejectsMissingSnapshot();
    void testExportRejectsInvalidOutputStem();
    void testExportRejectsMismatchedLayer();
    void testProjectMapperProducesQtDto();
};
