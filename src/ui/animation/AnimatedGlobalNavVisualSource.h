#pragma once
#include "ui/animation/AnimatedVisualSource.h"

#include <QPainterPath>

namespace manga::ui::animation {
/**
 * @brief Vector animation source dedicated to the global navigation menu
 * (hamburger button).
 * Mirrors the official WinUI 3 AnimatedGlobalNavigationButtonVisualSource
 * (Controls_02_Hamburger).
 *
 * In the Normal state it draws three horizontal lines of equal length; in the
 * Pressed state all three lines shrink inward horizontally in sync, rebounding
 * elastically with the Fluent deceleration curve.
 */
class AnimatedGlobalNavVisualSource : public AnimatedVisualSource {
public:
    int duration(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const override {
        if (to == IconState::Pressed)
            return anim.fast;
        return anim.normal;
    }

    QEasingCurve easing(IconState from, IconState to, const fluent::FluentElement::Animation& anim) const override {
        if (to == IconState::Pressed) {
            return anim.standard;
        }
        return anim.decelerate; // Smooth deceleration on release.
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

        // Current press interpolation (0.0 = normal, 1.0 = fully pressed).
        qreal pressRatio = 0.0;
        if (to == IconState::Pressed) {
            pressRatio = progress;
        } else if (from == IconState::Pressed) {
            pressRatio = 1.0 - progress;
        }

        const QPointF center = rect.center();
        painter.save();
        painter.translate(center);

        // Official precise math parameters of WinUI 3 Controls_02_Hamburger:
        // - 48x48 canvas; in the 16px viewport a full line is 15.0px long
        //   (halfWidth = 7.5px).
        // - Line spacing (Group Offset) is 4.85px (Y = -4.85, 0.0, +4.85).
        // - On press the Offset compresses from 37.5/10.5 to 27.5/20.5, a strict
        //   compression ratio of (10.0 / 24.0) = 41.67%; each end shrinks inward
        //   by 7.5 * 0.4167 = 3.125px, so the total length goes from 15.0px to
        //   8.75px (58.33% of the original).
        const qreal baseHalfWidth = 7.5;
        const qreal shrink = 3.125 * pressRatio;
        const qreal currentHalfWidth = baseHalfWidth - shrink;
        const qreal lineSpacing = 4.85;

        QPainterPath path;

        // Top line.
        path.moveTo(-currentHalfWidth, -lineSpacing);
        path.lineTo(currentHalfWidth, -lineSpacing);

        // Middle line.
        path.moveTo(-currentHalfWidth, 0.0);
        path.lineTo(currentHalfWidth, 0.0);

        // Bottom line.
        path.moveTo(-currentHalfWidth, lineSpacing);
        path.lineTo(currentHalfWidth, lineSpacing);

        QPen pen(currentColor, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        painter.restore();
    }
};
} // namespace manga::ui::animation
