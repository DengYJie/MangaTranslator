#pragma once

class QString;
class QWidget;

namespace manga::ui::navigation {

class NavigationPanel;

/// Registers navigation surfaces and routes. Implemented by NavigationSurfaceHost.
class INavigationRegistrar {
public:
    virtual ~INavigationRegistrar() = default;

    virtual void registerSurface(const QString& surfaceId, NavigationPanel* panel) = 0;
    virtual void registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId) = 0;
};

} // namespace manga::ui::navigation
