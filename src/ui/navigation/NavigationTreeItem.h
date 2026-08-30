#pragma once

#include "ui/navigation/NavigationPushButton.h"

#include <QAccessible>
#include <QString>

class QVariantAnimation;
class QMouseEvent;

namespace manga::ui::navigation {
class NavigationTreeWidget;

/**
 * @brief Navigation tree item.
 *
 * Supports leaf route items (route switching and indicator positioning) and
 * category items (accordion expansion with a chevron rotation animation).
 */
class NavigationTreeItem : public NavigationPushButton {
    Q_OBJECT
    Q_PROPERTY(float arrowAngle READ arrowAngle WRITE setArrowAngle)

public:
    /**
     * @brief Navigation item kinds.
     */
    enum class Kind {
        Leaf,     // Activatable leaf route item that switches pages.
        Category, // Category item that only organizes and expands/collapses children.
    };

    NavigationTreeItem(const QString& routeKey,
                       const QString& iconGlyph,
                       const QString& text,
                       const QString& tooltipText,
                       Kind kind,
                       int depth,
                       bool selectable = true,
                       QWidget* parent = nullptr,
                       std::shared_ptr<animation::AnimatedVisualSource> visualSource = nullptr);

    ~NavigationTreeItem() override = default;

    QString tooltipText() const {
        return m_tooltipText;
    }

    void setCompacted(bool compacted) override;

    QString routeKey() const {
        return m_routeKey;
    }
    Kind kind() const {
        return m_kind;
    }

    void setKind(Kind kind) {
        if (m_kind != kind) {
            m_kind = kind;
            update();
        }
    }

    bool isCategory() const {
        return m_kind == Kind::Category;
    }

    /// Owning node container (nullptr for leaf items inside a flyout).
    NavigationTreeWidget* treeParent() const {
        return m_treeParent;
    }
    void setTreeParent(NavigationTreeWidget* node) {
        m_treeParent = node;
    }

    QString accessibleItemName() const {
        return accessibleName();
    }

    void setAccessibleItemName(const QString& name);

    QAccessible::Role accessibleRole() const;

    /**
     * @brief Sets the category expansion state.
     * @param animated Whether to enable the smooth arrow rotation animation.
     */
    void setExpanded(bool expanded, bool animated = true);

    void setSelected(bool selected) override;

    void setOrientation(Qt::Orientation orientation) override;

    float arrowAngle() const {
        return m_arrowAngle;
    }

    void setArrowAngle(float angle);

    /// Only enabled after the flying indicator arrives at its target, so the
    /// persistent indicator is not painted early mid-flight.
    void setShowIndicator(bool show);

    // Plays the chevron rotation animation towards target; jumps directly when
    // the system reduced-motion setting is on.
    void animateChevron(float target);

    QRectF indicatorRect() const override;

Q_SIGNALS:
    /// Unified click source for the body, the chevron, and the keyboard arrow
    /// keys; chevronClicked tells whether the chevron hit area triggered it.

    void itemClicked(const QString& routeKey, bool chevronClicked);

protected:
    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    float currentTextAlpha() const override;

    int iconDrawX() const override;

    int textRightOffset() const override;

    QSize sizeHint() const override;

    QRectF chevronRect() const;

    bool chevronVisible() const;

private:
    QString m_tooltipText;

    void onClicked();

    void notifyAccessibleState(const QAccessible::State& state);

    void notifyAccessibleRoleChange();

private:
    QString m_routeKey;
    Kind m_kind;
    NavigationTreeWidget* m_treeParent = nullptr;
    bool m_showIndicator = false;
    bool m_chevronPressActive = false;
    float m_arrowAngle = 0.0f;
    QVariantAnimation* m_rotateAnimation = nullptr;
};
} // namespace manga::ui::navigation
