#pragma once

#include "application/ports/IRecentProjectsStore.h"

#include <QAbstractListModel>
#include <QVector>

namespace manga::ui::screen::home {

/// List model over IRecentProjectsStore snapshots. Replaced wholesale on refresh.
class RecentProjectsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole,
        PathRole,
        LastOpenedRole,
    };

    explicit RecentProjectsModel(QObject* parent = nullptr);

    void setProjects(const QVector<application::RecentProject>& projects);
    [[nodiscard]] const application::RecentProject* at(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
    QVector<application::RecentProject> m_projects;
};

} // namespace manga::ui::screen::home
