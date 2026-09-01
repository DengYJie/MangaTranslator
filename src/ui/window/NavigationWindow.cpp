#include "ui/window/NavigationWindow.h"

#include "ui/navigation/NavigationIndicator.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationToolButton.h"
#include "ui/navigation/NavigationWidget.h"
#include "ui/window/TitleBar.h"

#include <FluentQt/Navigation.h>
#include <QDebug>
#include <QMetaObject>

#include <utility>

namespace manga::ui::window {

NavigationWindow::NavigationWindow(QWidget* parent) : WindowBase(parent) {
    initNavigation();
}

NavigationWindow::~NavigationWindow() {
    // Members are destroyed before WindowBase destroys its child NavigationView
    // and NavigationPanel instances.  Disconnect panel callbacks first: the
    // destroyed() cleanup lambda captures this and must not touch m_surfaces
    // after the QHash member has been torn down.
    for (const auto& panel : std::as_const(m_surfaces)) {
        if (panel)
            QObject::disconnect(panel, nullptr, this, nullptr);
    }
    m_surfaces.clear();
    m_activeSurfaceId.clear();
}

void NavigationWindow::initNavigation() {
    m_navigationView = new fluent::navigation::NavigationView(this);
    // Main surface stays a fixed compact rail (shell spec §6.1); the pane toggle
    // expands it temporarily. Auto mode would widen it by window width.
    m_navigationView->setDisplayMode(fluent::navigation::NavigationView::DisplayMode::Left);
    m_navigationView->setPaneOpen(false);
    m_navigationView->setExpandedPaneWidth(256);

    // 1. Create the default main navigation surface.
    auto* mainPanel = new navigation::NavigationPanel(m_navigationView);
    registerSurface(QStringLiteral("main"), mainPanel);
    setActiveNavigationSurface(QStringLiteral("main"));

    // 2. Responsive and DisplayMode synchronization.
    connect(m_navigationView,
            &fluent::navigation::NavigationView::effectiveDisplayModeChanged,
            this,
            &NavigationWindow::syncActivePanelState);
    connect(m_navigationView,
            &fluent::navigation::NavigationView::paneOpenChanged,
            this,
            &NavigationWindow::syncActivePanelState);

    // 3. Unified TitleBar bindings.
    if (titleBar()) {
        titleBar()->setBackButtonVisible(false);
        titleBar()->setBackButtonEnabled(true);
        titleBar()->setPaneToggleButtonVisible(false);

        connect(titleBar(), &TitleBar::backButtonClicked, this, [this]() {
            if (m_navigationView) {
                using DisplayMode = fluent::navigation::NavigationView::DisplayMode;
                const auto mode = m_navigationView->effectiveDisplayMode();
                if (m_navigationView->isPaneOpen() &&
                    (mode == DisplayMode::LeftCompact || mode == DisplayMode::LeftMinimal)) {
                    m_navigationView->setPaneOpen(false);
                    return;
                }
            }
            Q_EMIT backRequested();
        });

        connect(titleBar(), &TitleBar::paneToggleButtonClicked, this, [this]() {
            if (m_navigationView) {
                m_navigationView->setPaneOpen(!m_navigationView->isPaneOpen());
            }
        });
    }

    // 4. ContentHost page-switch notification (dispatched through the queued
    // connection so the Controller transaction commits first).
    connect(m_navigationView->contentHost(),
            &fluent::navigation::StackContentHost::currentIndexChanged,
            this,
            [this](int index) {
                const QString routeKey = m_indexToRouteMap.value(index);
                if (!routeKey.isEmpty()) {
                    if (auto* panel = activeNavigationPanel()) {
                        panel->setCurrentItem(routeKey);
                    }
                    QMetaObject::invokeMethod(
                        this, [this, routeKey]() { Q_EMIT routeChanged(routeKey); }, Qt::QueuedConnection);
                }
            });

    syncActivePanelState();
    setContentWidget(m_navigationView);
}

bool NavigationWindow::registerSurface(const QString& surfaceId, navigation::NavigationPanel* panel) {
    if (surfaceId.isEmpty() || !panel)
        return false;

    if (m_surfaces.contains(surfaceId)) {
        auto existing = m_surfaces.value(surfaceId);
        if (existing.data() == panel) {
            return true;
        }
        if (existing) {
            qWarning().noquote()
                << "[NavigationWindow] registerSurface REJECTED: surfaceId already registered with active panel:"
                << surfaceId;
            Q_ASSERT_X(false, "NavigationWindow::registerSurface", "Duplicate surfaceId registered");
            return false;
        }
    }

    panel->setPaneToggleButtonVisible(false);
    panel->setBackButtonVisible(false);
    if (surfaceId != m_activeSurfaceId) {
        panel->hide();
    }

    connect(panel, &navigation::NavigationPanel::itemSelected, this, [this](const QString& routeKey) {
        Q_EMIT navigationRequested(routeKey);
    });

    connect(panel, &navigation::NavigationPanel::backRequested, this, [this]() {
        if (m_navigationView) {
            using DisplayMode = fluent::navigation::NavigationView::DisplayMode;
            const auto mode = m_navigationView->effectiveDisplayMode();
            if (m_navigationView->isPaneOpen() &&
                (mode == DisplayMode::LeftCompact || mode == DisplayMode::LeftMinimal)) {
                m_navigationView->setPaneOpen(false);
                return;
            }
        }
        Q_EMIT backRequested();
    });

    // Watch panel destruction; only run the cleanup when the pointer stored in the
    // map matches the panel being destroyed.
    connect(panel, &QObject::destroyed, this, [this, surfaceId, panel]() {
        if (m_surfaces.value(surfaceId).data() == panel || m_surfaces.value(surfaceId).isNull()) {
            m_surfaces.remove(surfaceId);
            if (m_activeSurfaceId == surfaceId) {
                m_activeSurfaceId.clear();
                if (m_surfaces.contains(QStringLiteral("main")) &&
                    m_surfaces.value(QStringLiteral("main")) != nullptr) {
                    setActiveNavigationSurface(QStringLiteral("main"));
                }
            }
        }
    });

    m_surfaces.insert(surfaceId, panel);
    return true;
}

bool NavigationWindow::registerRoute(const QString& routeKey, QWidget* page, const QString& surfaceId) {
    if (!m_navigationView || routeKey.isEmpty())
        return false;

    if (m_routes.contains(routeKey)) {
        qWarning().noquote() << "[NavigationWindow] registerRoute FAILED: duplicate routeKey:" << routeKey;
        Q_ASSERT_X(false, "NavigationWindow::registerRoute", "Duplicate route key registered");
        return false;
    }

    const QString targetSurfaceId = surfaceId.isEmpty() ? QStringLiteral("main") : surfaceId;
    if (!m_surfaces.contains(targetSurfaceId) || m_surfaces.value(targetSurfaceId) == nullptr) {
        qWarning().noquote() << "[NavigationWindow] registerRoute REJECTED: target surface not registered or destroyed:"
                             << targetSurfaceId << "for route:" << routeKey;
        Q_ASSERT_X(false, "NavigationWindow::registerRoute", "Surface must be registered before its routes");
        return false;
    }

    int index = -1;
    if (page) {
        auto* contentHost = m_navigationView->contentHost();
        index = contentHost->indexOf(page);
        if (index < 0) {
            index = contentHost->count();
            contentHost->insertPage(index, page);
            m_indexToRouteMap.insert(index, routeKey);
        }
    }

    navigation::RouteDescriptor desc;
    desc.routeKey = routeKey;
    desc.interfaceWidget = page;
    desc.surfaceId = targetSurfaceId;
    desc.contentIndex = index;

    m_routes.insert(routeKey, desc);
    return true;
}

bool NavigationWindow::addNavigationItem(const navigation::NavigationItemDescriptor& item, const QString& surfaceId) {
    auto* targetPanel = navigationPanel(surfaceId);
    if (!targetPanel) {
        qWarning().noquote() << "[NavigationWindow] addNavigationItem FAILED: surface not found:" << surfaceId;
        return false;
    }

    targetPanel->addItem(item.routeKey,
                         item.iconGlyph,
                         item.text,
                         item.parentRouteKey,
                         item.position,
                         item.selectable,
                         item.tooltip,
                         item.visualSource);
    return true;
}

bool NavigationWindow::addSubInterface(const QString& routeKey,
                                       QWidget* interfaceWidget,
                                       const QString& iconGlyph,
                                       const QString& text,
                                       const QString& parentRouteKey,
                                       navigation::NavigationItemPosition pos,
                                       bool selectable,
                                       std::shared_ptr<animation::AnimatedVisualSource> visualSource) {
    if (!registerRoute(routeKey, interfaceWidget, QStringLiteral("main"))) {
        return false;
    }

    navigation::NavigationItemDescriptor item;
    item.routeKey = routeKey;
    item.iconGlyph = iconGlyph;
    item.text = text;
    item.parentRouteKey = parentRouteKey;
    item.position = pos;
    item.selectable = selectable && (interfaceWidget != nullptr);
    item.visualSource = visualSource;

    return addNavigationItem(item, QStringLiteral("main"));
}

bool NavigationWindow::setActiveNavigationSurface(const QString& surfaceId) {
    if (surfaceId.isEmpty() || !m_navigationView)
        return false;
    auto targetPanel = m_surfaces.value(surfaceId);
    if (!targetPanel) {
        qWarning().noquote() << "[NavigationWindow] setActiveNavigationSurface FAILED: surface not found or destroyed:"
                             << surfaceId;
        m_surfaces.remove(surfaceId);
        if (m_activeSurfaceId == surfaceId) {
            m_activeSurfaceId.clear();
        }
        return false;
    }

    if (m_activeSurfaceId == surfaceId && m_navigationView->mainChromeWidget() == targetPanel.data()) {
        return true;
    }

    if (m_navigationView->mainChromeWidget()) {
        QWidget* oldWidget = m_navigationView->takeMainChromeWidget();
        if (oldWidget) {
            oldWidget->hide();
        }
    }

    m_navigationView->setMainChromeWidget(targetPanel.data(), fluent::WidgetOwnership::Reparented);
    m_activeSurfaceId = surfaceId;

    // Shell spec §6.1: all surfaces use the compact rail for consistent navigation.
    m_navigationView->setPaneOpen(false);

    syncActivePanelState();
    targetPanel->show();
    targetPanel->updateIndicatorVisuals(false);

    return true;
}

navigation::NavigationPanel* NavigationWindow::activeNavigationPanel() const {
    return m_surfaces.value(m_activeSurfaceId, nullptr).data();
}

navigation::NavigationPanel* NavigationWindow::mainNavigationPanel() const {
    return m_surfaces.value(QStringLiteral("main"), nullptr).data();
}

navigation::NavigationPanel* NavigationWindow::navigationPanel(const QString& surfaceId) const {
    return m_surfaces.value(surfaceId, nullptr).data();
}

void NavigationWindow::syncActivePanelState() {
    auto* panel = activeNavigationPanel();
    if (!panel || !m_navigationView)
        return;

    using DisplayMode = fluent::navigation::NavigationView::DisplayMode;
    using StackContentHost = fluent::navigation::StackContentHost;

    const auto mode = m_navigationView->effectiveDisplayMode();
    const bool top = (mode == DisplayMode::Top);

    panel->setOrientation(top ? Qt::Horizontal : Qt::Vertical);
    panel->setCompacted(!m_navigationView->isPaneOpen());

    if (auto* host = m_navigationView->contentHost()) {
        host->setTransitionEffect(top ? StackContentHost::TransitionEffect::SlideFromLeft
                                      : StackContentHost::TransitionEffect::SlideFromBottom);
    }
}

void NavigationWindow::addSectionHeader(const QString& text) {
    if (auto* panel = activeNavigationPanel()) {
        panel->addSectionHeader(text);
    }
}

void NavigationWindow::addWidget(navigation::NavigationWidget* widget, navigation::NavigationItemPosition position) {
    if (auto* panel = activeNavigationPanel()) {
        panel->addWidget(widget, position);
    }
}

bool NavigationWindow::canPresentRoute(const QString& routeKey) const {
    if (!m_navigationView || routeKey.isEmpty())
        return false;
    if (!m_routes.contains(routeKey))
        return false;

    const auto desc = m_routes.value(routeKey);
    if (!desc.surfaceId.isEmpty()) {
        if (!m_surfaces.contains(desc.surfaceId) || m_surfaces.value(desc.surfaceId) == nullptr) {
            return false;
        }
    }
    return true;
}

bool NavigationWindow::presentRoute(const QString& routeKey, navigation::NavigationDirection direction) {
    if (!canPresentRoute(routeKey)) {
        qWarning().noquote() << "[NavigationWindow] presentRoute REJECTED: cannot present route:" << routeKey;
        return false;
    }

    const auto desc = m_routes.value(routeKey);

    // 1. Strictly switch the surface first; abort immediately and return false on failure.
    if (!desc.surfaceId.isEmpty()) {
        if (!setActiveNavigationSurface(desc.surfaceId)) {
            qWarning().noquote() << "[NavigationWindow] present ABORTED: failed to activate surface:" << desc.surfaceId
                                 << "for route:" << routeKey;
            return false;
        }
    }

    // 2. Switch the content.
    if (desc.contentIndex >= 0) {
        auto* contentHost = m_navigationView->contentHost();
        const int currentIndex = contentHost->currentIndex();

        if (desc.contentIndex == currentIndex) {
            if (auto* panel = activeNavigationPanel()) {
                panel->setCurrentItem(routeKey);
            }
            QMetaObject::invokeMethod(
                this, [this, routeKey]() { Q_EMIT routeChanged(routeKey); }, Qt::QueuedConnection);
            return true;
        }

        // StackContentHost expects direction >= 0 for forward and < 0 for reverse;
        // NavigationDirection uses sequential enumerator values, so map explicitly.
        int animDirection = 0;
        switch (direction) {
            case navigation::NavigationDirection::Forward:
                animDirection = 1;
                break;
            case navigation::NavigationDirection::Back:
                animDirection = -1;
                break;
            case navigation::NavigationDirection::None:
                break;
        }
        const bool animated = (direction != navigation::NavigationDirection::None);

        contentHost->setCurrentIndex(desc.contentIndex, animDirection, animated);
    }

    return true;
}

void NavigationWindow::setPaneFooter(navigation::NavigationWidget* footerWidget) {
    if (auto* mainPanel = mainNavigationPanel()) {
        mainPanel->setPaneFooter(footerWidget);
    }
}

navigation::NavigationWidget* NavigationWindow::paneFooter() const {
    auto* mainPanel = mainNavigationPanel();
    return mainPanel ? mainPanel->paneFooter() : nullptr;
}

} // namespace manga::ui::window
