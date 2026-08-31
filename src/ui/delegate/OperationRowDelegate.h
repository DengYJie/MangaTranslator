#pragma once

#include "application/operation/OperationId.h"

#include <QStyledItemDelegate>

namespace fluent {
class FluentElement;
}

namespace manga::ui::delegate {

class OperationRowDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit OperationRowDelegate(fluent::FluentElement* themeSource, QObject* parent = nullptr);
    ~OperationRowDelegate() override = default;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

    [[nodiscard]] static QRect cancelButtonRect(const QRect& optionRect);

Q_SIGNALS:
    void cancelClicked(const manga::application::OperationId& id);

private:
    fluent::FluentElement* m_themeSource{nullptr}; // non-owning
};

} // namespace manga::ui::delegate
