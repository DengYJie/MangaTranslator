#include "ui/navigation/NavigationSurfaceHost.h"

#include "ui/navigation/NavigationPanel.h"

#include <FluentQt/Navigation.h>
#include <QVBoxLayout>

namespace manga::ui::navigation {

NavigationSurfaceHost::NavigationSurfaceHost(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_navigationView = new fluent::navigation::NavigationView(this);
    m_navigationView->setDisplayMode(fluent::navigation::NavigationView::DisplayMode::Left);
    m_navigationView->setPaneOpen(false); // Collapsed to the icon rail, the Left Compact intent of the shell spec.
    m_navigationView->contentHost()->setTransitionEffect(
        fluent::navigation::StackContentHost::TransitionEffect::SlideFromBottom);
    layout->addWidget(m_navigationView);
}

void NavigationSurfaceHost::registerSurface(const QString& surfaceId, NavigationPanel* panel) {
    if (surfaceId.isEmpty() || panel == nullptr || m_surfaces.contains(surfaceId)) {
        return;
    }
    panel->setParent(this); // Panel lifetime belongs to the host, not to the chrome take/set cycle.
    panel->hide();
    m_surfaces.insert(surfaceId, panel);
}

void NavigationSurfaceHost::registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId) {
    if (routeKey.isEmpty() || page == nullptr || !m_surfaces.contains(surfaceId) || m_routes.contains(routeKey)) {
        return;
    }
    auto* contentHost = m_navigationView->contentHost();
    const int index = static_cast<int>(m_routeOrder.size());
    contentHost->insertPage(index, page, fluent::WidgetOwnership::Reparented);
    m_routeOrder.append(routeKey);
    m_routes.insert(routeKey, RouteEntry{page, surfaceId, index});
}

bool NavigationSurfaceHost::canPresentRoute(const QString& routeKey) const {
    return m_routes.contains(routeKey);
}

bool NavigationSurfaceHost::presentRoute(const QString& routeKey, NavigationDirection direction) {
    const auto it = m_routes.constFind(routeKey);
    if (it == m_routes.constEnd()) {
        return false;
    }
    if (it->surfaceId != m_activeSurfaceId) {
        setActiveSurface(it->surfaceId);
    }
    if (auto* panel = m_surfaces.value(it->surfaceId)) {
        panel->setCurrentItem(routeKey);
    }
    // P5-A uses one uniform transition; Back/Forward differentiation lands with P5-D visual polish.
    const int transitionDirection = direction == NavigationDirection::Back ? -1 : 1;
    m_navigationView->contentHost()->setCurrentIndex(it->contentIndex, transitionDirection, true);
    m_currentRouteKey = routeKey;
    return true;
}

void NavigationSurfaceHost::setActiveSurface(const QString& surfaceId) {
    auto* next = m_surfaces.value(surfaceId, nullptr);
    if (next == nullptr) {
        return;
    }
    if (QWidget* previous = m_navigationView->takeMainChromeWidget()) {
        // takeMainChromeWidget detaches the widget from any parent; hand lifetime back to the host.
        previous->setParent(this);
        previous->hide();
    }
    m_navigationView->setMainChromeWidget(next, fluent::WidgetOwnership::Reparented);
    next->show();
    m_activeSurfaceId = surfaceId;
}

} // namespace manga::ui::navigation
