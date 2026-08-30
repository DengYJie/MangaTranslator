#pragma once

#include <QStyledItemDelegate>

namespace fluent {
class FluentElement;
}

namespace manga::ui::screen::home {

/// Paints one recent-project card: placeholder cover (16:10), name, and relative time.
/// Real cover thumbnails arrive with the thumbnail pipeline in P5-B.
class RecentProjectCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    /// themeSource supplies FluentQt semantic colors and must outlive the delegate
    /// (the hosting page passes itself).
    explicit RecentProjectCardDelegate(fluent::FluentElement* themeSource, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    fluent::FluentElement* m_themeSource; // non-owning
};

} // namespace manga::ui::screen::home
