#pragma once
#include <QObject>

class LayoutRegionUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulLayout();
    void testLayoutFailure();
    void testInvalidRequestDoesNotCallLayouter();
    void testCancellation();
};
