#pragma once

#include "application/ports/IRecentProjectsStore.h"

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
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
        CoverRole,
    };

    explicit RecentProjectsModel(QObject* parent = nullptr);

    void setProjects(const QVector<application::RecentProject>& projects);
    void setCovers(const QHash<QString, QImage>& covers);
    [[nodiscard]] const application::RecentProject* at(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

private:
    QVector<application::RecentProject> m_projects;
    QHash<QString, QImage> m_covers;
};

} // namespace manga::ui::screen::home
