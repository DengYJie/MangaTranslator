#pragma once

#include "ui/navigation/NavigationMetrics.h"

#include <QString>

#include <memory>

class QWidget;

namespace manga::ui::animation {
class AnimatedVisualSource;
}

namespace manga::ui::navigation {

/// Direction of a presented route, used for transition selection and history bookkeeping.
/// The numeric values double as the stack content host transition direction.
enum class NavigationDirection {
    Forward = 1,
    Back = -1,
    None = 0,
};

/// Route registration record: content page plus its hosting surface and stack index.
struct RouteDescriptor {
    QString routeKey;
    QWidget* interfaceWidget = nullptr; // non-owning; the content host owns the page
    QString surfaceId = QStringLiteral("main");
    int contentIndex = -1;
};

/// Navigation item registration record.
struct NavigationItemDescriptor {
    QString routeKey;
    QString iconGlyph;
    QString text;
    QString parentRouteKey;
    NavigationItemPosition position = NavigationItemPosition::Top;
    bool selectable = true;
    QString tooltip;
    std::shared_ptr<animation::AnimatedVisualSource> visualSource;
};

} // namespace manga::ui::navigation
