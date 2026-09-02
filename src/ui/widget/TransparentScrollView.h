#pragma once

#include <FluentQt/Scrolling.h>

namespace manga::ui::widget {

/// ScrollView variant whose viewport stays transparent instead of painting the FluentQt canvas color.
/// ScrollView::updateViewportPalette repaints the viewport with bgCanvas on every theme change, so the
/// transparent palette is restored afterwards. Use it for page-level content that shows the host surface.
class TransparentScrollView : public fluent::scrolling::ScrollView {
    Q_OBJECT
public:
    explicit TransparentScrollView(QWidget* parent = nullptr);

protected:
    void onThemeUpdated() override;

private:
    void keepViewportTransparent();
};

} // namespace manga::ui::widget
