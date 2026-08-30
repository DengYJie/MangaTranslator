#include "ui/animation/AnimatedIcon.h"

#include <FluentQt/Design.h>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace manga::ui::animation {
AnimatedIcon::AnimatedIcon(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_animation = new QVariantAnimation(this);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& val) {
        m_progress = val.toReal();
        update();
    });

    if (parent) {
        parent->installEventFilter(this);
        setGeometry(0, 0, parent->width(), parent->height());
    }
}

AnimatedIcon::~AnimatedIcon() = default;

void AnimatedIcon::setSource(std::shared_ptr<AnimatedVisualSource> source) {
    m_source = std::move(source);
    update();
}

void AnimatedIcon::setState(IconState newState) {
    if (m_currentState == newState || !m_source)
        return;

    qreal startProgress = 0.0;
    bool isReversing = false;
    const bool wasRunning = (m_animation->state() == QAbstractAnimation::Running);

    // If the previous animation has not finished and the new state equals the
    // source state (an interrupted return along the same path).
    if (wasRunning) {
        if (newState == m_previousState) {
            isReversing = true;
            // Reverse interpolation: the remaining travel fraction is exactly
            // 1.0 minus the current progress.
            startProgress = 1.0 - m_progress;
        }
    }

    m_previousState = m_currentState;
    m_currentState = newState;

    m_animation->stop();
    int duration = m_source->duration(m_previousState, m_currentState, themeAnimation());

    if (isReversing) {
        // Shorten the total duration proportionally to the remaining progress so
        // the transition speed stays consistent.
        duration = qMax(1, static_cast<int>(duration * (1.0 - startProgress)));
    }

    m_animation->setDuration(duration);
    m_animation->setEasingCurve(m_source->easing(m_previousState, m_currentState, themeAnimation()));
    m_animation->setStartValue(startProgress);
    m_animation->setEndValue(1.0);
    m_animation->start();
}

qreal AnimatedIcon::progress() const {
    return m_progress;
}

void AnimatedIcon::setProgress(qreal p) {
    m_progress = p;
    update();
}

void AnimatedIcon::paintEvent(QPaintEvent*) {
    if (!m_source)
        return;

    if (parentWidget() && m_currentState != IconState::Normal) {
        const bool under = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(QCursor::pos()));
        if (!under) {
            m_currentState = IconState::Normal;
            m_previousState = IconState::Normal;
            m_progress = 1.0;
        }
    }

    QPainter painter(this);
    m_source->paint(painter, rect(), m_previousState, m_currentState, m_progress, themeColorsRef(), isEnabled());
}

bool AnimatedIcon::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget()) {
        switch (event->type()) {
            case QEvent::Enter:
                setState(IconState::PointerOver);
                break;
            case QEvent::Leave:
                setState(IconState::Normal);
                break;
            case QEvent::MouseButtonPress:
                if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                    setState(IconState::Pressed);
                }
                break;
            case QEvent::MouseButtonRelease:
                if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                    const bool under = (parentWidget() &&
                                        parentWidget()->rect().contains(parentWidget()->mapFromGlobal(QCursor::pos())));
                    setState(under ? IconState::PointerOver : IconState::Normal);
                }
                break;
            case QEvent::Move:
            case QEvent::Resize:
                if (parentWidget()) {
                    if (m_autoFillParent) {
                        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
                    }
                    const bool under = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(QCursor::pos()));
                    if (!under && (m_currentState == IconState::PointerOver || m_currentState == IconState::Pressed)) {
                        setState(IconState::Normal);
                    }
                }
                break;
            case QEvent::Hide:
                // Reset the animation and return to Normal when the control hides.
                m_animation->stop();
                m_currentState = IconState::Normal;
                m_previousState = IconState::Normal;
                m_progress = 1.0;
                break;
            case QEvent::EnabledChange:
                update();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace manga::ui::animation
