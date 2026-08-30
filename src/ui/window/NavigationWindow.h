#pragma once

#include "WindowBase.h"
#include "ui/navigation/INavigationPresenter.h"
#include "ui/navigation/INavigationRegistrar.h"
#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationTypes.h"

#include <QHash>
#include <QPointer>
#include <QString>

#include <memory>

namespace manga::ui::animation {
class AnimatedVisualSource;
}

namespace manga::ui::navigation {
class NavigationPanel;
class NavigationIndicator;
class NavigationWidget;
} // namespace manga::ui::navigation

namespace fluent::navigation {
class NavigationView;
}

namespace manga::ui::window {

/**
 * @brief Navigation window base class: a pure UI shell container hosting multiple
 * navigation surfaces and presenting routed pages.
 */
class NavigationWindow : public WindowBase,
                         public navigation::INavigationPresenter,
                         public navigation::INavigationRegistrar {
    Q_OBJECT

public:
    explicit NavigationWindow(QWidget* parent = nullptr);
    ~NavigationWindow() override;

    bool registerSurface(const QString& surfaceId, navigation::NavigationPanel* panel) override;
    bool
    registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId = QStringLiteral("main")) override;
    bool addNavigationItem(const navigation::NavigationItemDescriptor& item,
                           const QString& surfaceId = QStringLiteral("main")) override;

    bool setActiveNavigationSurface(const QString& surfaceId);
    navigation::NavigationPanel* activeNavigationPanel() const;
    navigation::NavigationPanel* mainNavigationPanel() const;
    navigation::NavigationPanel* navigationPanel(const QString& surfaceId) const;
    QString activeNavigationSurfaceId() const {
        return m_activeSurfaceId;
    }

    /**
     * @brief Convenience shortcut registering a main-surface sub-page together with
     * its main navigation item.
     */
    bool addSubInterface(const QString& routeKey,
                         QWidget* interfaceWidget,
                         const QString& iconGlyph,
                         const QString& text,
                         const QString& parentRouteKey = QString(),
                         navigation::NavigationItemPosition pos = navigation::NavigationItemPosition::Top,
                         bool selectable = true,
                         std::shared_ptr<animation::AnimatedVisualSource> visualSource = nullptr);

    /**
     * @brief Adds a section header to the currently active navigation panel.
     * @param text Section title.
     */
    void addSectionHeader(const QString& text);

    /**
     * @brief Adds a custom widget to the currently active navigation panel.
     * @param widget Widget pointer.
     * @param position Layout position.
     */
    void addWidget(navigation::NavigationWidget* widget,
                   navigation::NavigationItemPosition position = navigation::NavigationItemPosition::Top);

    /**
     * @brief Sets the fixed footer widget of the main navigation panel.
     * @param footerWidget Footer widget pointer.
     */
    void setPaneFooter(navigation::NavigationWidget* footerWidget);

    /**
     * @brief Returns the fixed footer widget of the main navigation panel.
     * @return Footer widget pointer.
     */
    navigation::NavigationWidget* paneFooter() const;

    /**
     * @brief Returns the underlying Fluent NavigationView.
     * @return NavigationView pointer.
     */
    fluent::navigation::NavigationView* navigationView() const {
        return m_navigationView;
    }

    bool canPresentRoute(const QString& routeKey) const override;
    bool presentRoute(const QString& routeKey,
                      navigation::NavigationDirection direction = navigation::NavigationDirection::Forward) override;

Q_SIGNALS:
    /**
     * @brief A navigation intent was initiated by the user from UI such as the
     * navigation panel.
     * @param routeKey Target route.
     */
    void navigationRequested(const QString& routeKey);

    /**
     * @brief The user requested Back (TitleBar or Panel Back).
     */
    void backRequested();

    /**
     * @brief The user requested Forward.
     */
    void forwardRequested();

    /**
     * @brief A page has taken effect visually at the UI level.
     * @param routeKey The currently presented route.
     */
    void routeChanged(const QString& routeKey);

protected:
    void initNavigation();
    void syncActivePanelState();

protected:
    fluent::navigation::NavigationView* m_navigationView = nullptr;

    QHash<QString, QPointer<navigation::NavigationPanel>> m_surfaces;
    QString m_activeSurfaceId;

    QHash<QString, navigation::RouteDescriptor> m_routes;
    QHash<int, QString> m_indexToRouteMap;
};

} // namespace manga::ui::window
