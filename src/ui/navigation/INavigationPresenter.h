#pragma once

#include "ui/navigation/NavigationTypes.h"

class QString;

namespace manga::ui::navigation {

/// Renders routes in a content host. Implemented by NavigationSurfaceHost.
class INavigationPresenter {
public:
    virtual ~INavigationPresenter() = default;

    [[nodiscard]] virtual bool canPresentRoute(const QString& routeKey) const = 0;
    /// Returns true when the route became visible. False means history must not be committed.
    virtual bool presentRoute(const QString& routeKey, NavigationDirection direction) = 0;
};

} // namespace manga::ui::navigation
