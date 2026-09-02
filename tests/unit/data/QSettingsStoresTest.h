#pragma once

#include <QObject>
#include <QTemporaryDir>

class QSettingsStoresTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void appSettingsRoundTrip();
    void recentProjectsAreDeduplicatedSortedAndBounded();

private:
    QTemporaryDir m_tempDir;
};
