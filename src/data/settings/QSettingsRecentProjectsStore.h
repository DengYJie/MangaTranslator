#pragma once

#include "application/ports/IRecentProjectsStore.h"

namespace manga::data {

/// QSettings implementation of the recent projects port. It holds no state between calls because
/// every operation opens its own settings object, add and remove rewrite the whole stored group
/// rather than patching one entry, and clear drops the group outright. The group and entry key
/// names are persisted state and are documented where they are produced in the implementation file.
class QSettingsRecentProjectsStore final : public application::IRecentProjectsStore {
public:
    QSettingsRecentProjectsStore() = default;

    [[nodiscard]] QVector<application::RecentProject> loadRecentProjects() const override;
    [[nodiscard]] QFuture<application::Result<QImage>> loadProjectCover(QString path,
                                                                        application::OperationContext context) override;
    void addRecentProject(const application::RecentProject& project) override;
    void removeRecentProject(const QString& path) override;
    void clearRecentProjects() override;
};

} // namespace manga::data
