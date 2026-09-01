#pragma once

#include <FluentQt/Layout.h>

class QPaintEvent;

namespace manga::ui::widget {

/// Editor pane surface: Fluent Card token fill with square corners, so
/// neighbouring panes read as one continuous work surface separated only by
/// the splitter hairline instead of rounded-card gaps.
class PaneCard : public fluent::layout::Card {
    Q_OBJECT
public:
    explicit PaneCard(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace manga::ui::widget
