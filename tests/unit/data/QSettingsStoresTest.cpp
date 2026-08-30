#include "QSettingsStoresTest.h"

#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QtTest/QtTest>

#include <algorithm>

using namespace manga;

void QSettingsStoresTest::initTestCase() {
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("MangaTranslatorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("QSettingsStoresTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
}

void QSettingsStoresTest::init() {
    QSettings settings;
    settings.clear();
    settings.sync();
}

void QSettingsStoresTest::appSettingsRoundTrip() {
    data::QSettingsAppSettingsStore store;
    QVERIFY(!store.readSetting(QStringLiteral("provider/model")).has_value());

    store.writeSetting(QStringLiteral("provider/model"), QStringLiteral("gpt-test"));
    QCOMPARE(store.readSetting(QStringLiteral("provider/model"))->toString(), QStringLiteral("gpt-test"));

    store.removeSetting(QStringLiteral("provider/model"));
    QVERIFY(!store.readSetting(QStringLiteral("provider/model")).has_value());
}

void QSettingsStoresTest::recentProjectsAreDeduplicatedSortedAndBounded() {
    data::QSettingsRecentProjectsStore store;
    for (int index = 0; index < 25; ++index) {
        application::RecentProject project;
        project.name = QStringLiteral("Project %1").arg(index);
        project.path = QStringLiteral("C:/projects/%1").arg(index);
        project.lastOpenedTimeMs = index;
        store.addRecentProject(project);
    }

    auto projects = store.loadRecentProjects();
    QCOMPARE(projects.size(), 20);
    QCOMPARE(projects.front().path, QStringLiteral("C:/projects/24"));
    QCOMPARE(projects.back().path, QStringLiteral("C:/projects/5"));

    store.addRecentProject({QStringLiteral("Renamed"), QStringLiteral("C:/projects/10"), 1000});
    projects = store.loadRecentProjects();
    QCOMPARE(projects.size(), 20);
    QCOMPARE(projects.front().name, QStringLiteral("Renamed"));
    QCOMPARE(projects.front().path, QStringLiteral("C:/projects/10"));

    store.removeRecentProject(QStringLiteral("C:/projects/10"));
    projects = store.loadRecentProjects();
    QCOMPARE(projects.size(), 19);
    QVERIFY(std::ranges::none_of(projects,
                                 [](const auto& project) { return project.path == QStringLiteral("C:/projects/10"); }));

    store.clearRecentProjects();
    QVERIFY(store.loadRecentProjects().isEmpty());
}

#include "moc_QSettingsStoresTest.cpp"
