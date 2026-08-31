#include "ui/model/OperationListModel.h"

#include <QCoreApplication>

namespace manga::ui::model {

namespace {

bool areErrorsEqual(const std::optional<application::ApplicationError>& a,
                    const std::optional<application::ApplicationError>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    return a->category == b->category && a->code == b->code && a->safeMessage == b->safeMessage;
}

bool areSnapshotsEqual(const application::OperationSnapshot& a, const application::OperationSnapshot& b) {
    return a.id == b.id && a.state == b.state && a.completed == b.completed && a.total == b.total &&
           a.statusText == b.statusText && a.reasonCode == b.reasonCode && areErrorsEqual(a.error, b.error) &&
           a.descriptor.stage == b.descriptor.stage && a.descriptor.projectId == b.descriptor.projectId &&
           a.descriptor.pageId == b.descriptor.pageId && a.descriptor.regionId == b.descriptor.regionId &&
           a.descriptor.displayTarget == b.descriptor.displayTarget &&
           a.descriptor.cancellable == b.descriptor.cancellable;
}

} // namespace

OperationListModel::OperationListModel(QObject* parent) : QAbstractListModel(parent) {}

int OperationListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_operations.size();
}

QVariant OperationListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_operations.size()) {
        return {};
    }

    const auto& op = m_operations.at(index.row());

    switch (role) {
        case Qt::DisplayRole:
        case StatusTextRole:
            return op.statusText;
        case IdRole:
            return QVariant::fromValue(op.id);
        case ProjectIdRole:
            return QVariant::fromValue(op.descriptor.projectId);
        case PageIdRole:
            return QVariant::fromValue(op.descriptor.pageId);
        case RegionIdRole:
            return op.descriptor.regionId ? QVariant::fromValue(*op.descriptor.regionId) : QVariant();
        case StageRole:
            return QVariant::fromValue(op.descriptor.stage);
        case StageNameRole:
            return stageName(op.descriptor.stage);
        case DisplayTargetRole:
            return op.descriptor.displayTarget;
        case StateRole:
            return QVariant::fromValue(op.state);
        case StateNameRole:
            return stateName(op.state);
        case CompletedRole:
            return op.completed;
        case TotalRole:
            return op.total;
        case ProgressPercentRole:
            return (op.total > 0) ? static_cast<int>(op.completed * 100 / op.total) : 0;
        case CancellableRole:
            return op.descriptor.cancellable && (op.state == application::OperationState::Queued ||
                                                 op.state == application::OperationState::Running);
        case ErrorMessageRole:
            return op.error ? op.error->safeMessage : QString();
        case IsActiveRole:
            return op.state == application::OperationState::Queued ||
                   op.state == application::OperationState::Running ||
                   op.state == application::OperationState::Cancelling;
        case SnapshotRole:
            return QVariant::fromValue(op);
        default:
            return {};
    }
}

QHash<int, QByteArray> OperationListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {ProjectIdRole, "projectId"},
        {PageIdRole, "pageId"},
        {RegionIdRole, "regionId"},
        {StageRole, "stage"},
        {StageNameRole, "stageName"},
        {DisplayTargetRole, "displayTarget"},
        {StateRole, "state"},
        {StateNameRole, "stateName"},
        {CompletedRole, "completed"},
        {TotalRole, "total"},
        {ProgressPercentRole, "progressPercent"},
        {StatusTextRole, "statusText"},
        {CancellableRole, "cancellable"},
        {ErrorMessageRole, "errorMessage"},
        {IsActiveRole, "isActive"},
        {SnapshotRole, "snapshot"},
    };
}

void OperationListModel::setOperations(const QVector<application::OperationSnapshot>& operations) {
    if (m_operations.isEmpty() && operations.isEmpty()) {
        return;
    }

    if (m_operations.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, operations.size() - 1);
        m_operations = operations;
        endInsertRows();
        return;
    }

    if (operations.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_operations.size() - 1);
        m_operations.clear();
        endRemoveRows();
        return;
    }

    // Check if same item count and IDs in same order
    if (m_operations.size() == operations.size()) {
        bool allIdsMatch = true;
        for (int i = 0; i < m_operations.size(); ++i) {
            if (m_operations[i].id != operations[i].id) {
                allIdsMatch = false;
                break;
            }
        }
        if (allIdsMatch) {
            for (int i = 0; i < m_operations.size(); ++i) {
                if (!areSnapshotsEqual(m_operations[i], operations[i])) {
                    m_operations[i] = operations[i];
                    const QModelIndex idx = index(i, 0);
                    Q_EMIT dataChanged(idx, idx);
                }
            }
            return;
        }
    }

    // Check if new operations is an append (prefix match)
    if (operations.size() > m_operations.size()) {
        bool isPrefix = true;
        for (int i = 0; i < m_operations.size(); ++i) {
            if (m_operations[i].id != operations[i].id) {
                isPrefix = false;
                break;
            }
        }
        if (isPrefix) {
            for (int i = 0; i < m_operations.size(); ++i) {
                if (!areSnapshotsEqual(m_operations[i], operations[i])) {
                    m_operations[i] = operations[i];
                    const QModelIndex idx = index(i, 0);
                    Q_EMIT dataChanged(idx, idx);
                }
            }
            beginInsertRows(QModelIndex(), m_operations.size(), operations.size() - 1);
            for (int i = m_operations.size(); i < operations.size(); ++i) {
                m_operations.append(operations[i]);
            }
            endInsertRows();
            return;
        }
    }

    // Check single item removal at any position
    if (m_operations.size() - operations.size() == 1) {
        int removedIndex = -1;
        int i = 0;
        int j = 0;
        while (i < m_operations.size() && j < operations.size()) {
            if (m_operations[i].id == operations[j].id) {
                ++i;
                ++j;
            } else if (removedIndex == -1) {
                removedIndex = i;
                ++i;
            } else {
                removedIndex = -2;
                break;
            }
        }
        if (removedIndex == -1 && i == m_operations.size() - 1) {
            removedIndex = i; // Last item was removed
        }
        if (removedIndex >= 0) {
            beginRemoveRows(QModelIndex(), removedIndex, removedIndex);
            m_operations.removeAt(removedIndex);
            endRemoveRows();
            for (int k = 0; k < m_operations.size(); ++k) {
                if (!areSnapshotsEqual(m_operations[k], operations[k])) {
                    m_operations[k] = operations[k];
                    const QModelIndex idx = index(k, 0);
                    Q_EMIT dataChanged(idx, idx);
                }
            }
            return;
        }
    }

    // General fallback reset
    beginResetModel();
    m_operations = operations;
    endResetModel();
}

const QVector<application::OperationSnapshot>& OperationListModel::operations() const noexcept {
    return m_operations;
}

std::optional<application::OperationSnapshot> OperationListModel::operationAt(int row) const {
    if (row < 0 || row >= m_operations.size()) {
        return std::nullopt;
    }
    return m_operations.at(row);
}

std::optional<application::OperationSnapshot>
OperationListModel::findOperation(const application::OperationId& id) const {
    for (const auto& op : m_operations) {
        if (op.id == id) {
            return op;
        }
    }
    return std::nullopt;
}

QString OperationListModel::stageName(application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Project:
            return QCoreApplication::translate("OperationListModel", "项目");
        case application::OperationStage::Import:
            return QCoreApplication::translate("OperationListModel", "导入");
        case application::OperationStage::LoadImage:
            return QCoreApplication::translate("OperationListModel", "加载图像");
        case application::OperationStage::Ocr:
            return QCoreApplication::translate("OperationListModel", "OCR 识别");
        case application::OperationStage::Translate:
            return QCoreApplication::translate("OperationListModel", "AI 翻译");
        case application::OperationStage::Clear:
            return QCoreApplication::translate("OperationListModel", "文本消除");
        case application::OperationStage::Layout:
            return QCoreApplication::translate("OperationListModel", "文字排版");
        case application::OperationStage::Export:
            return QCoreApplication::translate("OperationListModel", "导出");
        default:
            return QCoreApplication::translate("OperationListModel", "任务");
    }
}

QString OperationListModel::stateName(application::OperationState state) {
    switch (state) {
        case application::OperationState::Queued:
            return QCoreApplication::translate("OperationListModel", "排队中");
        case application::OperationState::Running:
            return QCoreApplication::translate("OperationListModel", "处理中");
        case application::OperationState::Cancelling:
            return QCoreApplication::translate("OperationListModel", "正在停止");
        case application::OperationState::Completed:
            return QCoreApplication::translate("OperationListModel", "已完成");
        case application::OperationState::Skipped:
            return QCoreApplication::translate("OperationListModel", "已跳过");
        case application::OperationState::Failed:
            return QCoreApplication::translate("OperationListModel", "失败");
        case application::OperationState::Cancelled:
            return QCoreApplication::translate("OperationListModel", "已取消");
        case application::OperationState::Stale:
            return QCoreApplication::translate("OperationListModel", "已失效");
        default:
            return QString();
    }
}

} // namespace manga::ui::model
