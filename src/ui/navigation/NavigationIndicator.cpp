#include "ui/navigation/NavigationIndicator.h"

#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationWidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>
#include <QVariantAnimation>

namespace {
// WinUI indicator first-half easing: cubic-bezier(0.9,0.1,1.0,0.2), a fast dash
// that overshoots.
QEasingCurve winuiOvershoot() {
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(0.9, 0.1), QPointF(1.0, 0.2), QPointF(1.0, 1.0));
    return c;
}

// WinUI indicator second-half easing: cubic-bezier(0.1,0.9,0.2,1.0), the
// rebound settle.
QEasingCurve winuiSettle() {
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(0.1, 0.9), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
    return c;
}
} // namespace

namespace manga::ui::navigation {
fluent::FluentElement::Theme NavigationIndicator::cachedEffectiveTheme() const {
    if (!m_themeValid) {
        m_effectiveTheme = effectiveTheme();
        m_themeValid = true;
    }
    return m_effectiveTheme;
}

const fluent::FluentElement::Colors& NavigationIndicator::colorsRef() const {
    return fluent::ThemeRegistry::instance().colors(cachedEffectiveTheme() == fluent::FluentElement::Theme::Dark);
}

void NavigationIndicator::onThemeUpdated() {
    m_themeValid = false;
    update();
}

NavigationIndicator::NavigationIndicator(QWidget* parent)
    : QWidget(parent), m_startRect(0, 0, kSelectionIndicatorWidth, kSelectionIndicatorHeight),
      m_targetRect(m_startRect), m_currentRect(m_startRect) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    hide();

    m_flightAnimation = new QVariantAnimation(this);
    m_flightAnimation->setEasingCurve(themeAnimation().decelerate);
    connect(m_flightAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal p = v.toDouble();

        if (m_animMode == AnimationMode::PortalReturn) {
            // Left edge fixed, width grows from 0 to the target, simulating a
            // horizontal return.
            const qreal morphW = qMax(0.0, m_targetRect.width() * p);
            m_currentRect = QRectF(m_targetRect.x(), m_targetRect.y(), morphW, m_targetRect.height());
        } else if (m_animMode == AnimationMode::CrossWindowPortal) {
            // Shape dispatched by host orientation: the top bar shrinks, the
            // flyout grows.
            if (m_orientation == Qt::Horizontal) {
                // Left edge fixed, right edge moves left, width decays to 0.
                const qreal morphW = qMax(0.0, m_startRect.width() * (1.0 - p));
                m_currentRect = QRectF(m_startRect.x(), m_startRect.y(), morphW, m_startRect.height());
            } else {
                // Top edge fixed, height grows from 0 to the target.
                const qreal growH = m_targetRect.height() * p;
                m_currentRect = QRectF(m_targetRect.x(), m_targetRect.top(), m_targetRect.width(), growH);
            }
        } else if (m_orientation == Qt::Horizontal) {
            if (m_animMode == AnimationMode::Normal && m_sameAxisFlight) {
                // WinUI stretch-and-shift mechanism: the move lands quickly, then
                // the stretch rebounds.
                qreal scaleP = 0.0;
                if (p <= 1.0 / 3.0) {
                    scaleP = winuiOvershoot().valueForProgress(p * 3.0);
                    if (m_targetRect.x() >= m_startRect.x()) {
                        // Moving right.
                        m_currentRect.setLeft(m_startRect.left());
                        m_currentRect.setRight(m_startRect.right() +
                                               (m_targetRect.right() - m_startRect.right()) * scaleP);
                    } else {
                        // Moving left.
                        m_currentRect.setRight(m_startRect.right());
                        m_currentRect.setLeft(m_startRect.left() + (m_targetRect.left() - m_startRect.left()) * scaleP);
                    }
                } else {
                    scaleP = 1.0 - winuiSettle().valueForProgress((p - 1.0 / 3.0) * 1.5);
                    if (m_targetRect.x() >= m_startRect.x()) {
                        // Moving right.
                        m_currentRect.setRight(m_targetRect.right());
                        m_currentRect.setLeft(m_targetRect.left() -
                                              (m_targetRect.left() - m_startRect.left()) * scaleP);
                    } else {
                        // Moving left.
                        m_currentRect.setLeft(m_targetRect.left());
                        m_currentRect.setRight(m_targetRect.right() +
                                               (m_startRect.right() - m_targetRect.right()) * scaleP);
                    }
                }
                m_currentRect.setTop(m_targetRect.top());
                m_currentRect.setBottom(m_targetRect.bottom());
            } else {
                // Cross-axis/fallback animation: smooth move plus simple elastic stretch.
                const qreal moveP = p;
                const qreal stretchAmp = (p <= 1.0 / 3.0) ? (p * 3.0) : ((1.0 - p) * 1.5);
                const qreal current_x = m_startRect.x() + (m_targetRect.x() - m_startRect.x()) * moveP;
                const qreal width = m_startRect.width() + (m_targetRect.width() - m_startRect.width()) * moveP;
                const qreal stretch = stretchAmp * qMin(16.0, qAbs(m_targetRect.x() - m_startRect.x()) * 0.4);

                m_currentRect.setY(m_startRect.y() + (m_targetRect.y() - m_startRect.y()) * moveP);
                m_currentRect.setHeight(m_startRect.height() + (m_targetRect.height() - m_startRect.height()) * moveP);

                if (m_targetRect.x() >= m_startRect.x()) {
                    // Moving right.
                    m_currentRect.setLeft(current_x);
                    m_currentRect.setRight(current_x + width + stretch);
                } else {
                    // Moving left.
                    m_currentRect.setLeft(current_x - stretch);
                    m_currentRect.setRight(current_x + width);
                }
            }
        } else {
            // Left mode: use the portal effect when crossing levels (different X).
            const bool portalMode = qAbs(m_startRect.x() - m_targetRect.x()) > 0.5;
            const bool movingUp = m_targetRect.y() <= m_startRect.y();

            if (portalMode) {
                if (p < 0.5) {
                    const qreal lp = p * 2.0;
                    const qreal morphH = qMax(0.0, m_startRect.height() * (1.0 - lp));
                    m_currentRect.setX(m_startRect.x());
                    m_currentRect.setWidth(m_startRect.width());
                    if (movingUp) {
                        m_currentRect.setTop(m_startRect.top());
                        m_currentRect.setHeight(morphH);
                    } else {
                        m_currentRect.setTop(m_startRect.bottom() - morphH);
                        m_currentRect.setHeight(morphH);
                    }
                } else {
                    const qreal lp = (p - 0.5) * 2.0;
                    const qreal morphH = qMax(0.0, m_targetRect.height() * lp);
                    m_currentRect.setX(m_targetRect.x());
                    m_currentRect.setWidth(m_targetRect.width());
                    if (movingUp) {
                        m_currentRect.setTop(m_targetRect.bottom() - morphH);
                        m_currentRect.setHeight(morphH);
                    } else {
                        m_currentRect.setTop(m_targetRect.top());
                        m_currentRect.setHeight(morphH);
                    }
                }
            } else {
                if (m_animMode == AnimationMode::Normal && m_sameAxisFlight) {
                    qreal scaleP = 0.0;
                    if (p <= 1.0 / 3.0) {
                        scaleP = winuiOvershoot().valueForProgress(p * 3.0);
                        if (m_targetRect.y() >= m_startRect.y()) {
                            // Moving down.
                            m_currentRect.setTop(m_startRect.top());
                            m_currentRect.setBottom(m_startRect.bottom() +
                                                    (m_targetRect.bottom() - m_startRect.bottom()) * scaleP);
                        } else {
                            // Moving up.
                            m_currentRect.setBottom(m_startRect.bottom());
                            m_currentRect.setTop(m_startRect.top() + (m_targetRect.top() - m_startRect.top()) * scaleP);
                        }
                    } else {
                        scaleP = 1.0 - winuiSettle().valueForProgress((p - 1.0 / 3.0) * 1.5);
                        if (m_targetRect.y() >= m_startRect.y()) {
                            // Moving down.
                            m_currentRect.setBottom(m_targetRect.bottom());
                            m_currentRect.setTop(m_targetRect.top() -
                                                 (m_targetRect.top() - m_startRect.top()) * scaleP);
                        } else {
                            // Moving up.
                            m_currentRect.setTop(m_targetRect.top());
                            m_currentRect.setBottom(m_targetRect.bottom() +
                                                    (m_startRect.bottom() - m_targetRect.bottom()) * scaleP);
                        }
                    }
                    m_currentRect.setLeft(m_targetRect.left());
                    m_currentRect.setRight(m_targetRect.right());
                } else {
                    const qreal moveP = p;
                    const qreal stretchAmp = (p <= 1.0 / 3.0) ? (p * 3.0) : ((1.0 - p) * 1.5);
                    const qreal current_y = m_startRect.y() + (m_targetRect.y() - m_startRect.y()) * moveP;
                    const qreal height = m_startRect.height() + (m_targetRect.height() - m_startRect.height()) * moveP;
                    const qreal stretch = stretchAmp * qMin(16.0, qAbs(m_targetRect.y() - m_startRect.y()) * 0.4);

                    m_currentRect.setX(m_startRect.x() + (m_targetRect.x() - m_startRect.x()) * moveP);
                    m_currentRect.setWidth(m_startRect.width() + (m_targetRect.width() - m_startRect.width()) * moveP);

                    if (m_targetRect.y() >= m_startRect.y()) {
                        // Moving down.
                        m_currentRect.setTop(current_y);
                        m_currentRect.setBottom(current_y + height + stretch);
                    } else {
                        // Moving up.
                        m_currentRect.setTop(current_y - stretch);
                        m_currentRect.setBottom(current_y + height);
                    }
                }
            }
        }

        // Buffer the geometry by one margin so the stretch beyond the target
        // rectangle is not clipped.
        setGeometry(m_startRect.united(m_targetRect).toRect().adjusted(-20, -20, 20, 20));
        update();
    });
    connect(m_flightAnimation, &QVariantAnimation::finished, this, [this]() {
        const AnimationMode finishedMode = m_animMode;
        // Normalize to the target geometry so the animated and non-animated paths
        // end identically and later isSamePosition checks are not misled.
        m_animMode = AnimationMode::Normal;
        m_startRect = m_targetRect;
        m_currentRect = m_targetRect;
        hide();
        Q_EMIT flightFinished();
    });
}

void NavigationIndicator::setOrientation(Qt::Orientation orientation) {
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;
    update();
}

void NavigationIndicator::setInitialPosition(const QRectF& rect) {
    m_startRect = rect;
    m_targetRect = rect;
    m_currentRect = rect;
    setGeometry(rect.toRect().adjusted(-5, -5, 5, 5));
}

void NavigationIndicator::activateAt(const QRectF& targetRect, bool animated) {
    m_animMode = AnimationMode::Normal;
    const bool samePos = isSamePosition(targetRect);

    if (samePos) {
        // Position unchanged: stop the animation and hide the floating indicator
        // so the item restores its persistent indicator.
        m_flightAnimation->stop();
        m_startRect = targetRect;
        m_targetRect = targetRect;
        m_currentRect = targetRect;
        hide();
        Q_EMIT flightStarted();
        Q_EMIT flightFinished();
        return;
    }

    raiseToTop();

    if (!animated || NavigationWidget::isReducedMotion()) {
        m_flightAnimation->stop();
        m_startRect = targetRect;
        m_targetRect = targetRect;
        m_currentRect = targetRect;
        hide();
        Q_EMIT flightStarted();
        Q_EMIT flightFinished();
        return;
    }

    // If a previous flight was interrupted mid-animation, derive an undistorted
    // standard geometry from the current center position as the start.
    if (m_flightAnimation && m_flightAnimation->state() == QAbstractAnimation::Running) {
        if (m_orientation == Qt::Vertical) {
            const qreal cy = m_currentRect.center().y();
            m_startRect =
                QRectF(targetRect.x(), cy - targetRect.height() / 2.0, targetRect.width(), targetRect.height());
        } else {
            const qreal cx = m_currentRect.center().x();
            m_startRect =
                QRectF(cx - targetRect.width() / 2.0, targetRect.y(), targetRect.width(), targetRect.height());
        }
    } else {
        m_startRect = m_currentRect.isValid() ? m_currentRect : targetRect;
    }

    m_targetRect = targetRect;
    // Same-axis translations (same x or same y) use the manually segmented
    // cubic-bezier easing to mimic the WinUI stretch; cross-axis portal keeps the
    // smooth decelerate as-is.
    m_sameAxisFlight = qAbs(m_startRect.x() - m_targetRect.x()) < 0.5 || qAbs(m_startRect.y() - m_targetRect.y()) < 0.5;

    if (m_sameAxisFlight) {
        beginFlight(QEasingCurve::Linear, 600);
    } else {
        beginFlight(themeAnimation().decelerate);
    }
    setGeometry(m_startRect.united(m_targetRect).toRect().adjusted(-20, -20, 20, 20));
    show();
    Q_EMIT flightStarted();
    m_flightAnimation->start();
}

void NavigationIndicator::playPortalReturn(const QRectF& targetRect) {
    if (NavigationWidget::isReducedMotion()) {
        m_startRect = targetRect;
        m_targetRect = targetRect;
        m_currentRect = targetRect;
        hide();
        Q_EMIT flightStarted();
        Q_EMIT flightFinished();
        return;
    }

    raiseToTop();
    m_animMode = AnimationMode::PortalReturn;
    // Start position: target width collapsed to 0.
    m_startRect = QRectF(targetRect.x(), targetRect.y(), 0, targetRect.height());

    m_targetRect = targetRect;
    m_currentRect = m_startRect;
    beginFlight(themeAnimation().decelerate, themeAnimation().fast);
    setGeometry(targetRect.toRect().adjusted(-5, -5, 5, 5));
    show();
    Q_EMIT flightStarted();
    m_flightAnimation->start();
}

void NavigationIndicator::playCrossWindowPortal(const QRectF& startRect, const QRectF& targetRect) {
    if (NavigationWidget::isReducedMotion()) {
        m_startRect = targetRect;
        m_targetRect = targetRect;
        m_currentRect = targetRect;
        hide();
        Q_EMIT flightStarted();
        // The Portal animation does not emit flightFinished (each caller handles
        // its own completion).
        return;
    }

    raiseToTop();
    m_animMode = AnimationMode::CrossWindowPortal;
    m_startRect = startRect;
    m_targetRect = targetRect;
    beginFlight(themeAnimation().decelerate, themeAnimation().fast);

    // Geometry buffering and the initial frame are dispatched by host
    // orientation: the top bar shrink only needs the start rectangle; the flyout
    // growing downward (top fixed, height 0) only needs to cover the target
    // rectangle.
    if (m_orientation == Qt::Horizontal) {
        m_currentRect = m_startRect;
        setGeometry(m_startRect.toRect().adjusted(-5, -5, 5, 5));
    } else {
        // Initial frame with the top edge fixed and height 0.
        m_currentRect = QRectF(m_targetRect.x(), m_targetRect.top(), m_targetRect.width(), 0.0);
        setGeometry(m_targetRect.toRect().adjusted(-5, -5, 5, 5));
    }
    show();
    Q_EMIT flightStarted();
    m_flightAnimation->start();
}

void NavigationIndicator::hideIndicator() {
    if (m_flightAnimation)
        m_flightAnimation->stop();
    hide();
}

bool NavigationIndicator::isFlying() const {
    return m_flightAnimation && m_flightAnimation->state() == QAbstractAnimation::Running;
}

bool NavigationIndicator::isSamePosition(const QRectF& targetRect) const {
    // Treat target geometry within <1px of the current geometry as unchanged and
    // short-circuit to avoid pointless animation.
    return qAbs(targetRect.x() - m_currentRect.x()) < 1.0 && qAbs(targetRect.y() - m_currentRect.y()) < 1.0;
}

void NavigationIndicator::raiseToTop() {
    raise();
}

void NavigationIndicator::beginFlight(const QEasingCurve& easing, int durationMs) {
    m_flightAnimation->stop();
    m_flightAnimation->setEasingCurve(easing);
    m_flightAnimation->setStartValue(0.0);
    m_flightAnimation->setEndValue(1.0);
    m_flightAnimation->setDuration(durationMs > 0 ? durationMs : themeAnimation().normal);
}

void NavigationIndicator::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colorsRef().accentDefault);

    // The geometry is one margin larger than the actual rect (-5,-5,5,5), so map
    // back to local coordinates.
    const QPointF offset = -geometry().topLeft();
    const QRectF local = m_currentRect.translated(offset);

    painter.drawRoundedRect(local.adjusted(0, 0, -1, -1), ::CornerRadius::Indicator, ::CornerRadius::Indicator);
}

void NavigationIndicator::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    raiseToTop();
}
} // namespace manga::ui::navigation
