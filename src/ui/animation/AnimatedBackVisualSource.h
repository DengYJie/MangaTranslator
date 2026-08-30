#pragma once
#include "ui/animation/AnimatedVisualSource.h"

#include <QPainterPath>
#include <QtMath>

#include <cmath>

namespace manga::ui::animation {
/**
 * @brief Vector animation source dedicated to the back button.
 * Mirrors the official WinUI 3 AnimatedBackVisualSource (Controls_03_Back).
 *
 * 1. Strictly follows the WinUI 3 native 48x48 vector proportions (Chevron opening
 *    angle ~88 degrees, wing length 10.3px, stem length 12px).
 * 2. On Pressed: the arrow compresses to the right (Chevron shifts +3.0px, stem
 *    right end shifts +0.31px).
 * 3. On Release: triggers the classic WinUI 3 slingshot effect, overshooting
 *    -1.34px to the left before smoothly settling back.
 * 4. On Hover: the color stays textPrimary without change.
 */
class AnimatedBackVisualSource : public AnimatedVisualSource {
public:
    int duration(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const override {
        if (to == IconState::Pressed)
            return anim.fast; // 150ms fast press compression
        if (from == IconState::Pressed)
            return 320; // 320ms spring rebound and overshoot
        return anim.normal;
    }

    QEasingCurve easing(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const override {
        if (to == IconState::Pressed) {
            return anim.standard;
        }
        return QEasingCurve::Linear; // Release uses the precise WinUI 3 two-segment bezier/spring interpolation
                                     // internally.
    }

    void paint(QPainter& painter,
               const QRectF& rect,
               IconState from,
               IconState to,
               qreal progress,
               const fluent::FluentElement::Colors& colors,
               bool isEnabled) override {
        painter.setRenderHint(QPainter::Antialiasing);

        // WinUI 3 spec: no color change on hover; textPrimary normally, textDisabled when disabled.
        const QColor currentColor = isEnabled ? colors.textPrimary : colors.textDisabled;

        qreal chevronOffset = 0.0;
        qreal stemRightOffset = 0.0;

        if (to == IconState::Pressed) {
            // Press phase (0.0 -> 1.0): squeeze to the right.
            // WinUI 3 proportions: Chevron shifts +3.0px, stem right end shifts +0.3125px.
            const qreal t = std::sin(progress * M_PI / 2.0); // decelerating press
            chevronOffset = 3.0 * t;
            stemRightOffset = 0.3125 * t;
        } else if (from == IconState::Pressed) {
            // Release phase (0.0 -> 1.0): WinUI 3 spring rebound (Slingshot Overshoot).
            // First half (0.0 -> 0.5): snaps back from +3.0px and overshoots to
            // -1.34px (stem right end overshoots to -1.25px).
            // Second half (0.5 -> 1.0): decelerates from the overshoot position
            // back to 0.0px.
            if (progress < 0.5) {
                const qreal u = progress * 2.0;
                const qreal t = std::sin(u * M_PI / 2.0);
                chevronOffset = 3.0 + (-1.34 - 3.0) * t;
                stemRightOffset = 0.3125 + (-1.25 - 0.3125) * t;
            } else {
                const qreal u = (progress - 0.5) * 2.0;
                const qreal t = std::sin(u * M_PI / 2.0);
                chevronOffset = -1.34 * (1.0 - t);
                stemRightOffset = -1.25 * (1.0 - t);
            }
        }

        const QPointF center = rect.center();
        painter.save();
        painter.translate(center);

        // Precise coordinates (based on WinUI 3 Geometry_0, 1, 2, scale-mapped to
        // the 16px standard grid).
        const qreal chevronX = -6.0 + chevronOffset;
        const qreal tipTopX = -0.628 + chevronOffset;
        const qreal tipBottomX = -0.628 + chevronOffset;
        const qreal stemLeftX = -4.0 + chevronOffset;
        const qreal stemRightX = 6.0 + stemRightOffset;

        QPainterPath path;

        // 1. V-shaped arrowhead (Chevron).
        path.moveTo(tipTopX, -5.15);
        path.lineTo(chevronX, 0.0);
        path.lineTo(tipBottomX, 5.15);

        // 2. Horizontal arrow stem (Stem).
        path.moveTo(stemLeftX, 0.0);
        path.lineTo(stemRightX, 0.0);

        QPen pen(currentColor, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        painter.restore();
    }
};
} // namespace manga::ui::animation
