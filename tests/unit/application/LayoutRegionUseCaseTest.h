#pragma once
#include <QObject>

class LayoutRegionUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulLayout();
    void testLayoutFailure();
    void testInvalidRequestDoesNotCallLayouter();
    void testCancellation();
};
