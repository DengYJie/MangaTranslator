#pragma once

#include "application/operation/OperationSnapshot.h"
#include "domain/model/Uuid.h"

#include <QAbstractListModel>
#include <QVector>

#include <optional>

namespace manga::ui::model {

class OperationListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ProjectIdRole,
        PageIdRole,
        RegionIdRole,
        StageRole,
        StageNameRole,
        DisplayTargetRole,
        StateRole,
        StateNameRole,
        CompletedRole,
        TotalRole,
        ProgressPercentRole,
        StatusTextRole,
        CancellableRole,
        ErrorMessageRole,
        IsActiveRole,
        SnapshotRole
    };
    Q_ENUM(Roles)

    explicit OperationListModel(QObject* parent = nullptr);
    ~OperationListModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setOperations(const QVector<application::OperationSnapshot>& operations);
    [[nodiscard]] const QVector<application::OperationSnapshot>& operations() const noexcept;
    [[nodiscard]] std::optional<application::OperationSnapshot> operationAt(int row) const;
    [[nodiscard]] std::optional<application::OperationSnapshot> findOperation(const application::OperationId& id) const;

    [[nodiscard]] static QString stageName(application::OperationStage stage);
    [[nodiscard]] static QString stateName(application::OperationState state);

private:
    QVector<application::OperationSnapshot> m_operations;
};

} // namespace manga::ui::model
