#pragma once

#include "ui/navigation/NavigationHistory.h"

#include <QObject>

namespace manga::ui::navigation {

class INavigationPresenter;

/// Single decision entry for shell navigation: guards reentrancy, rejects unknown routes,
/// and commits history only after the presenter rendered the route.
class NavigationController : public QObject {
    Q_OBJECT
public:
    explicit NavigationController(INavigationPresenter* presenter, QObject* parent = nullptr);

    void navigate(const QString& routeKey);
    void goBack();
    void goForward();

    [[nodiscard]] bool canGoBack() const {
        return m_history.canGoBack();
    }
    [[nodiscard]] bool canGoForward() const {
        return m_history.canGoForward();
    }

signals:
    void routeChanged(const QString& routeKey);
    void canGoBackChanged(bool canGoBack);
    void canGoForwardChanged(bool canGoForward);

private:
    void emitAvailabilityChanges();

    INavigationPresenter* m_presenter; // non-owning
    NavigationHistory m_history;
    bool m_navigating{false};
};

} // namespace manga::ui::navigation
