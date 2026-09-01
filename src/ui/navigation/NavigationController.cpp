#include "ui/navigation/NavigationController.h"

#include "ui/navigation/INavigationPresenter.h"

namespace manga::ui::navigation {

NavigationController::NavigationController(INavigationPresenter* presenter, QObject* parent)
    : QObject(parent), m_presenter(presenter) {}

void NavigationController::navigate(const QString& routeKey) {
    if (m_navigating || m_presenter == nullptr || routeKey.isEmpty()) {
        return;
    }
    if (m_history.current() == routeKey) {
        return;
    }
    if (!m_presenter->canPresentRoute(routeKey)) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(routeKey, NavigationDirection::Forward);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.push(routeKey);
    Q_EMIT routeChanged(routeKey);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

void NavigationController::goBack() {
    const auto target = m_history.backTarget();
    if (!target || m_navigating || m_presenter == nullptr) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(*target, NavigationDirection::Back);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.commitBack();
    Q_EMIT routeChanged(*target);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

void NavigationController::goForward() {
    const auto target = m_history.forwardTarget();
    if (!target || m_navigating || m_presenter == nullptr) {
        return;
    }
    m_navigating = true;
    const bool presented = m_presenter->presentRoute(*target, NavigationDirection::Forward);
    m_navigating = false;
    if (!presented) {
        return;
    }
    const bool couldGoBack = m_history.canGoBack();
    const bool couldGoForward = m_history.canGoForward();
    m_history.commitForward();
    Q_EMIT routeChanged(*target);
    if (couldGoBack != m_history.canGoBack()) {
        Q_EMIT canGoBackChanged(m_history.canGoBack());
    }
    if (couldGoForward != m_history.canGoForward()) {
        Q_EMIT canGoForwardChanged(m_history.canGoForward());
    }
}

} // namespace manga::ui::navigation
