#include "ui/navigation/NavigationToolButton.h"

#include "ui/navigation/NavigationMetrics.h"

#include <QMoveEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>

namespace manga::ui::navigation {
NavigationToolButton::NavigationToolButton(const QString& iconGlyph, QWidget* parent)
    : NavigationPushButton(iconGlyph, QString(), false, parent) {
    setFixedSize(kTopBarItemHeight, kTopBarItemHeight);
    setFocusPolicy(Qt::TabFocus);
}

void NavigationToolButton::setAnimatedMove(bool enabled) {
    if (m_animatedMove == enabled)
        return;
    m_animatedMove = enabled;
    if (enabled) {
        if (!m_moveAnimation) {
            m_moveAnimation = new QVariantAnimation(this);
            // Matches the WinUI overflow button Offset implicit animation:
            // 200ms + cubic-bezier(0,0.35,0.15,1) ease-out.
            QEasingCurve curve(QEasingCurve::BezierSpline);
            curve.addCubicBezierSegment(QPointF(0.0, 0.35), QPointF(0.15, 1.0), QPointF(1.0, 1.0));
            m_moveAnimation->setEasingCurve(curve);
            m_moveAnimation->setDuration(200);
            // The manual move() drives the position synchronously: move triggers
            // moveEvent immediately, m_animating is set/cleared around it so the
            // event is recognized as an animation frame, and resets when move
            // returns with no residue.
            connect(m_moveAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
                m_animating = true;
                move(v.toPoint());
                m_animating = false;
            });
        }
    } else if (m_moveAnimation) {
        m_moveAnimation->stop();
    }
}

void NavigationToolButton::runPendingMove() {
    m_moveScheduled = false;
    // The just-shown flag only applies to the first layout pass after becoming
    // visible again; it resets once the animation has been scheduled.
    m_justShown = false;
    if (m_pendingFrom == m_pendingTarget)
        return;
    m_moveAnimation->stop();
    m_moveAnimation->setStartValue(m_pendingFrom);
    m_moveAnimation->setEndValue(m_pendingTarget);
    m_moveAnimation->start();
}

void NavigationToolButton::moveEvent(QMoveEvent* event) {
    // Callback from the animation's manual move() (valueChanged sets the flag
    // synchronously): let it through, do not treat it as a layout reflow.
    if (m_animating) {
        m_animating = false;
        QWidget::moveEvent(event);
        return;
    }

    // Move animation disabled: defer to the base class without interfering.
    if (!m_animatedMove || !m_moveAnimation) {
        QWidget::moveEvent(event);
        return;
    }

    // Layout reflow: record the start point (first event) and the latest target,
    // then run a single unified animation after the whole layout pass settles,
    // avoiding jitter from one animation per move event during a single reflow.
    if (!m_moveScheduled) {
        m_moveScheduled = true;
        const QPoint from = event->oldPos();
        // Invalid initial coordinate (undefined on first layout) or just restored
        // from hidden: land directly instead of flying in from stale coordinates.
        if (from.isNull() || from.x() < 0 || m_justShown) {
            m_pendingFrom = event->pos();
            m_pendingTarget = event->pos();
        } else {
            m_pendingFrom = from;
        }
        QTimer::singleShot(0, this, [this] { runPendingMove(); });
    }
    m_pendingTarget = event->pos();

    QWidget::moveEvent(event);
}

void NavigationToolButton::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // When restoring from hidden, oldPos still holds the last visible coordinates;
    // flag it so this layout pass starts from the target, not the stale value.
    m_justShown = true;
}

void NavigationToolButton::setCompacted(bool /*compacted*/) {
    // Icon-only buttons keep their size and icon position in compact mode.
}

void NavigationToolButton::setOrientation(Qt::Orientation orientation) {
    NavigationPushButton::setOrientation(orientation);
    // The base class setFixedHeight by orientation would override the fixed size;
    // restore the 48x48 square hit area here.
    setFixedSize(kTopBarItemHeight, kTopBarItemHeight);
}

int NavigationToolButton::iconDrawX() const {
    return qMax(0, (width() - m_iconSize) / 2);
}
} // namespace manga::ui::navigation
