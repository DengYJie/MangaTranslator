#pragma once

#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace manga::application {

struct RecentProject {
    QString name;
    QString path;
    qint64 lastOpenedTimeMs{0};
    bool operator==(const RecentProject&) const = default;
};

/// Narrow port for managing the recent projects list.
/// This abstracts the persistence mechanism, for example QSettings, a database, or JSON.
class IRecentProjectsStore {
public:
    virtual ~IRecentProjectsStore() = default;

    /// Loads the list of recent projects, typically ordered by lastOpenedTimeMs descending.
    [[nodiscard]] virtual QVector<RecentProject> loadRecentProjects() const = 0;

    /// Called on the GUI thread and completed on any thread. Owns copies of the path and
    /// context and returns an owned first-page preview. A null image means no cover is available.
    /// Cancellation is checked before I/O and after decoding and returns cancelled.user_requested.
    /// Unexpected adapter exceptions return internal.unexpected_exception.
    [[nodiscard]] virtual QFuture<Result<QImage>> loadProjectCover(QString path, OperationContext context) {
        Q_UNUSED(path);
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<QImage>(makeApplicationError(ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.user_requested"),
                                                                 QStringLiteral("Cover loading was cancelled")));
        }
        return makeReadyFuture<QImage>(QImage{});
    }

    /// Adds a project to the recent list or updates its lastOpenedTimeMs if it already exists.
    virtual void addRecentProject(const RecentProject& project) = 0;

    /// Removes a project from the recent list by its path.
    virtual void removeRecentProject(const QString& path) = 0;

    /// Discards every stored entry at once so a later loadRecentProjects returns an empty list.
    virtual void clearRecentProjects() = 0;
};

} // namespace manga::application
