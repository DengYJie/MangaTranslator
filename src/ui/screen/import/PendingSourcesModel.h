#pragma once

#include "ui/screen/import/ImportViewModel.h"

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace manga::ui::screen::import_ {

class PendingSourcesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PathRole = Qt::UserRole,
        DisplayNameRole,
        IsFolderRole,
        SupportedRole,
        ErrorSummaryRole
    };

    explicit PendingSourcesModel(ImportViewModel* viewModel, QObject* parent = nullptr);
    ~PendingSourcesModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    ImportViewModel* m_viewModel;
};

} // namespace manga::ui::screen::import_
