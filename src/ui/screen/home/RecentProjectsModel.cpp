#include "ui/screen/home/RecentProjectsModel.h"

#include "ui/screen/home/RelativeTime.h"

#include <QDateTime>

namespace manga::ui::screen::home {

RecentProjectsModel::RecentProjectsModel(QObject* parent) : QAbstractListModel(parent) {}

void RecentProjectsModel::setProjects(const QVector<application::RecentProject>& projects) {
    if (m_projects == projects) {
        return;
    }
    beginResetModel();
    m_projects = projects;
    endResetModel();
}

void RecentProjectsModel::setCovers(const QHash<QString, QImage>& covers) {
    const auto previous = m_covers;
    m_covers = covers;
    for (int row = 0; row < m_projects.size(); ++row) {
        const auto& path = m_projects.at(row).path;
        if (previous.value(path) != m_covers.value(path)) {
            const auto item = index(row);
            Q_EMIT dataChanged(item, item, {CoverRole});
        }
    }
}

const application::RecentProject* RecentProjectsModel::at(int row) const {
    if (row < 0 || row >= m_projects.size()) {
        return nullptr;
    }
    return &m_projects.at(row);
}

int RecentProjectsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_projects.size());
}

QVariant RecentProjectsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_projects.size()) {
        return {};
    }

    const auto& project = m_projects.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return project.name;
        case Qt::ToolTipRole:
        case PathRole:
            return project.path;
        case LastOpenedRole:
            return project.lastOpenedTimeMs;
        case CoverRole:
            return m_covers.value(project.path);
        case Qt::AccessibleTextRole:
            return tr("项目 %1，路径 %2，上次打开 %3")
                .arg(project.name,
                     project.path,
                     formatRelativeTime(project.lastOpenedTimeMs, QDateTime::currentMSecsSinceEpoch()));
        default:
            return {};
    }
}

} // namespace manga::ui::screen::home
