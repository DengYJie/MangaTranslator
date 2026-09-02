#pragma once
#include <QObject>

class LoadPageImageUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulLoad();
    void testLoadFailure();
    void testLoaderFailurePropagates();
    void testPreCancelledLoadRejected();
};
