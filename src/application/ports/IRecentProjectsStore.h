#pragma once

#include <QString>
#include <QVector>

namespace manga::application {

struct RecentProject {
    QString name;
    QString path;
    qint64 lastOpenedTimeMs{0};
};

/// Narrow port for managing the recent projects list.
/// This abstracts the persistence mechanism (e.g. QSettings, database, JSON) for recent projects.
class IRecentProjectsStore {
public:
    virtual ~IRecentProjectsStore() = default;

    /// Loads the list of recent projects, typically ordered by lastOpenedTimeMs descending.
    [[nodiscard]] virtual QVector<RecentProject> loadRecentProjects() const = 0;

    /// Adds a project to the recent list or updates its lastOpenedTimeMs if it already exists.
    virtual void addRecentProject(const RecentProject& project) = 0;

    /// Removes a project from the recent list by its path.
    virtual void removeRecentProject(const QString& path) = 0;

    /// Clears the entire list of recent projects.
    virtual void clearRecentProjects() = 0;
};

} // namespace manga::application
