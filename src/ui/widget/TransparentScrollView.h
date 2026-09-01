#pragma once

#include <FluentQt/Scrolling.h>

namespace manga::ui::widget {

/// ScrollView 变体：viewport 始终保持透明，不绘制 fluent 默认的画布背景色
/// （ScrollView::updateViewportPalette 会把 viewport 涂成 bgCanvas）。
/// 适合承载页面级内容（如设置页），让背景透出宿主页面。
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
