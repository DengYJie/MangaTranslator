#pragma once
#include <QObject>

class ExportPagesUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulExport();
    void testInvalidOptionsRejected();
    void testExporterFailurePropagates();
    void testCancellation();
};
