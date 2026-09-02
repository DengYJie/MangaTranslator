#pragma once

#include <QObject>
#include <QString>

class WindowsCredentialStoreTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void roundTripStoresReadsAndDeletes();
    void overwritingReplacesTheKeyImmediately();
    void deleteIsIdempotent();
    void rejectsInvalidProviderId();
    void rejectsEmptyAndNullContainingKeys();

private:
    QString m_providerId;
};
