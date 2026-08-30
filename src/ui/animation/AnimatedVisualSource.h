#pragma once

#include <FluentQt/Foundation.h>
#include <QEasingCurve>
#include <QPainter>

namespace manga::ui::animation {
enum class IconState {
    Normal,
    PointerOver,
    Pressed,
    Disabled
};

/**
 * @brief Abstract animated visual source interface.
 *
 * Paints vector animation graphics from the animation progress (0.0 to 1.0)
 * together with the source and target states.
 */
class AnimatedVisualSource {
public:
    virtual ~AnimatedVisualSource() = default;

    /**
     * @brief Returns the transition animation duration for a state change.
     * @param from Source state.
     * @param to Target state.
     * @param anim Animation configuration token.
     * @return Animation duration in milliseconds.
     */
    virtual int duration(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const {
        return 150;
    }

    /**
     * @brief Returns the easing curve for a state change.
     * @param from Source state.
     * @param to Target state.
     * @param anim Animation configuration token.
     * @return Easing curve.
     */
    virtual QEasingCurve easing(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const {
        return QEasingCurve::OutCubic;
    }

    /**
     * @brief Paints the vector graphic at the current progress.
     * @param painter QPainter paint device context.
     * @param rect Icon paint area.
     * @param from Source state.
     * @param to Target state.
     * @param progress Animation progress [0.0, 1.0].
     * @param colors Theme color tokens.
     * @param isEnabled Whether the component is enabled.
     */
    virtual void paint(QPainter& painter,
                       const QRectF& rect,
                       IconState from,
                       IconState to,
                       qreal progress,
                       const fluent::FluentElement::Colors& colors,
                       bool isEnabled) = 0;
};
} // namespace manga::ui::animation
