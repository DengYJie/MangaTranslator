#pragma once

#include "ui/window/NavigationWindow.h"

namespace manga::application {
class IAppSettingsStore;
}

namespace manga::ui::navigation {
class NavigationController;
}

namespace manga::ui::window {

/// Top-level shell window: NavigationWindow plus geometry persistence and
/// back/forward wiring against the navigation controller.
class MainWindow : public NavigationWindow {
    Q_OBJECT
public:
    explicit MainWindow(application::IAppSettingsStore* settingsStore, QWidget* parent = nullptr);

    /// Wires the controller to the title bar and panel intents. Non-owning.
    void setNavigationController(navigation::NavigationController* controller);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void restoreWindowGeometry();
    void saveWindowGeometry();

    application::IAppSettingsStore* m_settingsStore;         // non-owning
    navigation::NavigationController* m_controller{nullptr}; // non-owning
};

} // namespace manga::ui::window
