#pragma once

#include "ui/navigation/NavigationWidget.h"

namespace manga::ui::navigation {

/// Navigation region separator.
///
/// Provides a Fluent-conformant 1px visual separation between semantic regions
/// such as Main and Footer.
class NavigationSeparator : public NavigationWidget {
    Q_OBJECT

public:
    explicit NavigationSeparator(QWidget* parent = nullptr);
    ~NavigationSeparator() override = default;

    void setOrientation(Qt::Orientation orientation) override;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace manga::ui::navigation
