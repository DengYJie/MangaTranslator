#include "ui/screen/import/PendingSourcesModel.h"

namespace manga::ui::screen::import_ {

PendingSourcesModel::PendingSourcesModel(ImportViewModel* viewModel, QObject* parent)
    : QAbstractListModel(parent), m_viewModel(viewModel) {
    connect(m_viewModel, &ImportViewModel::stateChanged, this, [this]() {
        beginResetModel();
        endResetModel();
    });
}

PendingSourcesModel::~PendingSourcesModel() = default;

int PendingSourcesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_viewModel->state().sources.size();
}

QVariant PendingSourcesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_viewModel->state().sources.size()) {
        return {};
    }

    const auto& source = m_viewModel->state().sources.at(index.row());

    switch (role) {
        case PathRole:
            return source.path;
        case DisplayNameRole:
            return source.displayName;
        case IsFolderRole:
            return source.isFolder;
        case SupportedRole:
            return source.supported;
        case ErrorSummaryRole:
            return source.errorSummary;
        default:
            return {};
    }
}

QHash<int, QByteArray> PendingSourcesModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[PathRole] = "path";
    roles[DisplayNameRole] = "displayName";
    roles[IsFolderRole] = "isFolder";
    roles[SupportedRole] = "supported";
    roles[ErrorSummaryRole] = "errorSummary";
    return roles;
}

} // namespace manga::ui::screen::import_
