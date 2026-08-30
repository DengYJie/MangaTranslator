#pragma once

#include <FluentQt/Foundation.h>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QVariantAnimation;

namespace manga::ui::navigation {
class NavigationWidget;
class NavigationTreeItem;
class NavigationTreeWidget;

/**
 * @brief Navigation tree node container base class.
 *
 * Abstracts the node container's "structure + visual item + expansion"
 * responsibilities: each node holds a visual item (itemWidget), a child node list,
 * and a collapsible child container. Root resources (scroll view, indicator,
 * overflow, global index) are carried by the derived NavigationTreeWidget; nodes
 * reach back to the root through m_root for global operations (selection /
 * indicator / flyout).
 */
class NavigationTreeWidgetBase : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    ~NavigationTreeWidgetBase() override;

    /// Category state derives from the children: with children it is a category,
    /// without children it is a leaf.
    bool isCategory() const {
        return !m_children.isEmpty();
    }

    /// Strongly typed access to the visual item.
    NavigationTreeItem* item();

    const NavigationTreeItem* item() const;

    QString routeKey() const {
        return m_routeKey;
    }
    NavigationWidget* itemWidget() const {
        return m_itemWidget;
    }
    const QVector<NavigationTreeWidget*>& children() const {
        return m_children;
    }

    NavigationTreeWidget* parentNode() const;

    /// Collapses/expands this node: accordion semantics, synchronizing the
    /// itemWidget arrow and the child container height.
    void setExpanded(bool expanded, bool animated = true);

    /// Expands all ancestor categories along the parent chain so the target node
    /// becomes visible.
    void expandAncestors(NavigationTreeWidget* node);

    /// Decision slot: unified entry for the itemWidget itemClicked signal,
    /// dispatched by category/selectable/mode.
protected:
    void onItemClicked(const QString& routeKey, bool chevronClicked);

public:
Q_SIGNALS:
    /// Expansion state change, letting the root handle side effects such as
    /// indicator ownership uniformly.

    void expansionChanged(const QString& routeKey, bool expanded);

protected:
    explicit NavigationTreeWidgetBase(NavigationTreeWidget* root, QWidget* parent = nullptr);

    NavigationTreeWidget* root();

    const NavigationTreeWidget* root() const;

    /// Collapsed child container height animation: per node, driving
    /// setFixedHeight from a normalized progress.
    void animateChildrenContainer(bool expanded);

    // Node container members.
    /// Root instance: the root points to itself; QPointer guards against dangling.
    QPointer<NavigationTreeWidget> m_root;
    /// Visual item: a leaf may be any NavigationWidget.
    NavigationWidget* m_itemWidget = nullptr;
    QString m_routeKey;
    QPointer<NavigationTreeWidget> m_parentNode;
    QVector<NavigationTreeWidget*> m_children;
    QWidget* m_childrenContainer = nullptr;
    QVBoxLayout* m_childrenLayout = nullptr;
    bool m_isExpanded = false;
    bool m_rememberExpandState = false;
    bool m_savedExpanded = false;
    /// Per-node collapsed height animation.
    QVariantAnimation* m_heightAnimation = nullptr;
};
} // namespace manga::ui::navigation
