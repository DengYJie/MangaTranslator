#pragma once
#include <QObject>

class LoadPageImageUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulLoad();
    void testLoadFailure();
    void testLoaderFailurePropagates();
    void testPreCancelledLoadRejected();
};
