#pragma once
#include "ui/animation/AnimatedVisualSource.h"

#include <FluentQt/Foundation.h>
#include <QWidget>

#include <memory>

class QVariantAnimation;

namespace manga::ui::animation {
/**
 * @brief Icon container component with vector state animation.
 *
 * Works with AnimatedVisualSource to animate smooth micro-deformation
 * transitions between icon states.
 */
class AnimatedIcon : public QWidget, public fluent::FluentElement {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit AnimatedIcon(QWidget* parent = nullptr);

    ~AnimatedIcon() override;

    void onThemeUpdated() override {
        update();
    }

    /**
     * @brief Sets the animated visual source.
     * @param source Vector animation source instance.
     */
    void setSource(std::shared_ptr<AnimatedVisualSource> source);

    /**
     * @brief Triggers a state change and runs the corresponding transition animation.
     * @param newState Target state.
     */
    void setState(IconState newState);

    /**
     * @brief Returns the current icon state.
     * @return Current state.
     */
    IconState state() const {
        return m_currentState;
    }

    /**
     * @brief Returns the current animation progress.
     * @return Progress [0.0, 1.0].
     */
    qreal progress() const;

    /**
     * @brief Sets the current animation progress.
     * @param p Progress [0.0, 1.0].
     */
    void setProgress(qreal p);

    /**
     * @brief Sets whether the icon automatically fills its parent container.
     * @param autoFill When true the icon keeps filling the parent on resize; when
     * false its geometry is controlled externally.
     */
    void setAutoFillParent(bool autoFill) {
        m_autoFillParent = autoFill;
    }

    /**
     * @brief Returns whether the icon automatically fills its parent container.
     * @return Whether it auto-fills the parent.
     */
    bool autoFillParent() const {
        return m_autoFillParent;
    }

protected:
    void paintEvent(QPaintEvent* event) override;

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::shared_ptr<AnimatedVisualSource> m_source;
    IconState m_previousState = IconState::Normal;
    IconState m_currentState = IconState::Normal;
    qreal m_progress = 1.0;
    bool m_autoFillParent = true;
    QVariantAnimation* m_animation = nullptr;
};
} // namespace manga::ui::animation
