#pragma once

#include "application/ports/IRecentProjectsStore.h"

#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::home {

struct HomeNotice {
    enum class Severity {
        Info,
        Error,
    };

    Severity severity{Severity::Info};
    QString message;
    QString retryPath; // Empty means no retry action.

    bool operator==(const HomeNotice&) const = default;
};

struct HomeState {
    QVector<application::RecentProject> recentProjects;
    bool isOpeningProject{false};
    std::optional<HomeNotice> notice;

    bool operator==(const HomeState& other) const {
        if (isOpeningProject != other.isOpeningProject || notice != other.notice ||
            recentProjects.size() != other.recentProjects.size()) {
            return false;
        }
        for (qsizetype i = 0; i < recentProjects.size(); ++i) {
            const auto& a = recentProjects.at(i);
            const auto& b = other.recentProjects.at(i);
            if (a.name != b.name || a.path != b.path || a.lastOpenedTimeMs != b.lastOpenedTimeMs) {
                return false;
            }
        }
        return true;
    }
};

} // namespace manga::ui::screen::home
