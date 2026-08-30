#pragma once

#include "ui/navigation/INavigationPresenter.h"
#include "ui/navigation/INavigationRegistrar.h"

#include <FluentQt/Foundation.h>
#include <QHash>
#include <QVector>
#include <QWidget>

namespace fluent::navigation {
class NavigationView;
}

namespace manga::ui::navigation {

/// Owns the single NavigationView chrome. Surfaces swap their navigation panel through
/// take/setMainChromeWidget; all pages live in one shared StackContentHost.
class NavigationSurfaceHost : public QWidget,
                              public fluent::FluentElement,
                              public fluent::QMLPlus,
                              public INavigationRegistrar,
                              public INavigationPresenter {
    Q_OBJECT
public:
    explicit NavigationSurfaceHost(QWidget* parent = nullptr);
    ~NavigationSurfaceHost() override = default;

    void registerSurface(const QString& surfaceId, NavigationPanel* panel) override;
    void registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId) override;

    [[nodiscard]] bool canPresentRoute(const QString& routeKey) const override;
    bool presentRoute(const QString& routeKey, NavigationDirection direction) override;

    [[nodiscard]] QString activeSurfaceId() const {
        return m_activeSurfaceId;
    }
    [[nodiscard]] QString currentRouteKey() const {
        return m_currentRouteKey;
    }

private:
    struct RouteEntry {
        QWidget* page{nullptr}; // reparented into the content host
        QString surfaceId;
        int contentIndex{-1};
    };

    void setActiveSurface(const QString& surfaceId);

    fluent::navigation::NavigationView* m_navigationView{nullptr};
    QHash<QString, NavigationPanel*> m_surfaces; // panels stay children of this host
    QHash<QString, RouteEntry> m_routes;
    QVector<QString> m_routeOrder; // content index <-> routeKey mapping
    QString m_activeSurfaceId;
    QString m_currentRouteKey;
};

} // namespace manga::ui::navigation
