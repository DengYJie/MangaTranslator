#pragma once

#include "application/ports/IRecentProjectsStore.h"

namespace manga::data {

class QSettingsRecentProjectsStore final : public application::IRecentProjectsStore {
public:
    QSettingsRecentProjectsStore() = default;

    [[nodiscard]] QVector<application::RecentProject> loadRecentProjects() const override;
    void addRecentProject(const application::RecentProject& project) override;
    void removeRecentProject(const QString& path) override;
    void clearRecentProjects() override;
};

} // namespace manga::data
