#pragma once

#include "ui/navigation/NavigationFocusHost.h"
#include "ui/navigation/NavigationMetrics.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QColor>
#include <QMargins>
#include <QRectF>
#include <QVariantAnimation>
#include <QWidget>

namespace manga::ui::navigation {
/**
 * @brief Navigation item base class.
 *
 * Encapsulates the selected state, hover transitions, accessibility semantics, the
 * keyboard focus ring, and hierarchy indentation, providing a unified Fluent
 * interaction baseline for all navigation entries.
 */
class NavigationWidget : public QWidget, public fluent::FluentElement {
    Q_OBJECT
    Q_PROPERTY(float hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit NavigationWidget(bool isSelectable = true, QWidget* parent = nullptr);

    ~NavigationWidget() override = default;

    /**
     * @brief Detects whether the system accessibility reduced-motion setting is on.
     * @return true if the user disabled animations in the system-level settings.
     */
    static bool isReducedMotion();

    /**
     * @brief Global input modality awareness: whether physical keyboard navigation
     * mode is currently active.
     */
    static bool isKeyboardMode();

    bool isCompacted() const {
        return m_orientation == Qt::Vertical && m_isCompacted;
    }

    virtual void setCompacted(bool compacted);

    bool isSelected() const {
        return m_isSelected;
    }

    virtual void setSelected(bool selected);

    bool isSelectable() const {
        return m_isSelectable;
    }
    void setSelectable(bool selectable) {
        m_isSelectable = selectable;
    }

    NavigationItemPosition itemPosition() const {
        return m_itemPosition;
    }
    void setItemPosition(NavigationItemPosition pos) {
        m_itemPosition = pos;
    }
    bool isFooterItem() const {
        return m_itemPosition == NavigationItemPosition::Bottom;
    }

    int nodeDepth() const {
        return m_nodeDepth;
    }

    void setNodeDepth(int depth);

    float hoverProgress() const {
        return m_hoverProgress;
    }

    void setHoverProgress(float progress);

    Qt::Orientation orientation() const {
        return m_orientation;
    }

    virtual void setOrientation(Qt::Orientation orientation);

    virtual QRectF indicatorRect() const;

    void click();

    /**
     * @brief Walks the parent chain to find the focus host.
     * @return Host pointer implementing INavigationFocusHost, used to forward
     * arrow-key focus movement upward.
     */
    INavigationFocusHost* navigationFocusHost() const;

    void setAccessibleItemName(const QString& name);

    void onThemeUpdated() override;

protected:
    /// Resolves once along the parent chain on first use or after invalidation,
    /// then caches the result.
    fluent::FluentElement::Theme cachedEffectiveTheme() const;

    /// Avoids the per-call parent-chain traversal of effectiveTheme() in
    /// themeColorsRef(); the reference is owned by ThemeRegistry and is only used
    /// within a single paint() pass.
    const fluent::FluentElement::Colors& colorsRef() const;

Q_SIGNALS:

    void clicked(bool triggerByUser);

protected:
    void enterEvent(FluentEnterEvent* event) override;

    void leaveEvent(QEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    void focusInEvent(QFocusEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

    QColor currentBackgroundColor() const;

    virtual float currentTextAlpha() const;

    /// Draws the two-layer solid high-contrast keyboard focus ring.
    void drawFocusVisual(QPainter& painter, const QRectF& rect) const;

protected:
    bool m_isCompacted = false;
    bool m_isSelected = false;
    bool m_isPressed = false;
    bool m_isHovered = false;
    /// Set to false for category items and action buttons.
    bool m_isSelectable = true;
    NavigationItemPosition m_itemPosition = NavigationItemPosition::Top;
    /// Used for stepped indentation.
    int m_nodeDepth = 0;

    float m_hoverProgress = 0.0f;
    Qt::Orientation m_orientation = Qt::Vertical;

    QVariantAnimation* m_hoverAnimation = nullptr;

    mutable fluent::FluentElement::Theme m_effectiveTheme = fluent::FluentElement::Theme::Light;
    /// Invalidated after a theme change.
    mutable bool m_themeValid = false;
};
} // namespace manga::ui::navigation
