#pragma once

#include <FluentQt/Windowing.h>

namespace manga::application {
class IAppSettingsStore;
}

namespace manga::ui::navigation {
class NavigationController;
class NavigationSurfaceHost;
} // namespace manga::ui::navigation

namespace fluent::basicinput {
class Button;
}

namespace manga::ui::window {

/// Top-level shell: Fluent Window chrome, title bar zones, geometry persistence.
class MainWindow : public fluent::windowing::Window {
    Q_OBJECT
public:
    MainWindow(application::IAppSettingsStore* settingsStore,
               navigation::NavigationSurfaceHost* host,
               navigation::NavigationController* controller,
               QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildTitleBar();
    void restoreWindowGeometry();
    void saveWindowGeometry();

    application::IAppSettingsStore* m_settingsStore; // non-owning
    navigation::NavigationController* m_controller;  // non-owning
    fluent::basicinput::Button* m_backButton{nullptr};
};

} // namespace manga::ui::window
