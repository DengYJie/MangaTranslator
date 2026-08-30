#pragma once

#include "ui/navigation/NavigationTypes.h"

#include <QString>

class QWidget;

namespace manga::ui::navigation {

class NavigationPanel;

/// Registers navigation surfaces, routes, and items. Implemented by NavigationWindow.
class INavigationRegistrar {
public:
    virtual ~INavigationRegistrar() = default;

    /// Registers a navigation surface panel. On success the host container owns the panel.
    virtual bool registerSurface(const QString& surfaceId, NavigationPanel* panel) = 0;

    /// Registers a route and its content page widget.
    virtual bool
    registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId = QStringLiteral("main")) = 0;

    /// Adds a navigation item to the given surface.
    virtual bool addNavigationItem(const NavigationItemDescriptor& item,
                                   const QString& surfaceId = QStringLiteral("main")) = 0;
};

} // namespace manga::ui::navigation
