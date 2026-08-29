#include "QSettingsRecentProjectsStore.h"

#include <QSettings>

#include <algorithm>

namespace manga::data {

namespace {
const QString kRecentProjectsKey = QStringLiteral("recent_projects");
}

QVector<application::RecentProject> QSettingsRecentProjectsStore::loadRecentProjects() const {
    QSettings settings;
    QVector<application::RecentProject> projects;

    int size = settings.beginReadArray(kRecentProjectsKey);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        application::RecentProject p;
        p.name = settings.value(QStringLiteral("name")).toString();
        p.path = settings.value(QStringLiteral("path")).toString();
        p.lastOpenedTimeMs = settings.value(QStringLiteral("lastOpenedTimeMs")).toLongLong();
        projects.append(p);
    }
    settings.endArray();

    // Sort by last opened time descending
    std::sort(projects.begin(), projects.end(), [](const auto& a, const auto& b) {
        return a.lastOpenedTimeMs > b.lastOpenedTimeMs;
    });

    return projects;
}

void QSettingsRecentProjectsStore::addRecentProject(const application::RecentProject& project) {
    auto projects = loadRecentProjects();

    auto it = std::find_if(projects.begin(), projects.end(), [&](const auto& p) { return p.path == project.path; });

    if (it != projects.end()) {
        it->lastOpenedTimeMs = project.lastOpenedTimeMs;
        it->name = project.name; // Update name in case it changed
    } else {
        projects.prepend(project);
    }

    // Keep max 20 projects
    if (projects.size() > 20) {
        projects.resize(20);
    }

    QSettings settings;
    settings.beginWriteArray(kRecentProjectsKey);
    for (int i = 0; i < projects.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), projects[i].name);
        settings.setValue(QStringLiteral("path"), projects[i].path);
        settings.setValue(QStringLiteral("lastOpenedTimeMs"), projects[i].lastOpenedTimeMs);
    }
    settings.endArray();
}

void QSettingsRecentProjectsStore::removeRecentProject(const QString& path) {
    auto projects = loadRecentProjects();

    projects.erase(std::remove_if(projects.begin(), projects.end(), [&](const auto& p) { return p.path == path; }),
                   projects.end());

    QSettings settings;
    settings.beginWriteArray(kRecentProjectsKey);
    for (int i = 0; i < projects.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), projects[i].name);
        settings.setValue(QStringLiteral("path"), projects[i].path);
        settings.setValue(QStringLiteral("lastOpenedTimeMs"), projects[i].lastOpenedTimeMs);
    }
    settings.endArray();
}

void QSettingsRecentProjectsStore::clearRecentProjects() {
    QSettings settings;
    settings.remove(kRecentProjectsKey);
}

} // namespace manga::data
