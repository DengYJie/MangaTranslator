#pragma once

#include "ui/window/NavigationWindow.h"

#include <FluentQt/TextFields.h>

namespace fluent::menus_toolbars {
class FluentMenuBar;
}

namespace manga::application {
class IAppSettingsStore;
}

namespace manga::ui::navigation {
class NavigationController;
}

namespace manga::ui::widget {
class AppStatusTaskStrip;
}

namespace manga::ui::screen::editor {
struct EditorState;
}

namespace manga::ui::window {

/// Top-level shell window: NavigationWindow plus geometry persistence and
/// back/forward wiring against the navigation controller.
class MainWindow : public NavigationWindow {
    Q_OBJECT
public:
    explicit MainWindow(application::IAppSettingsStore* settingsStore, QWidget* parent = nullptr);

    [[nodiscard]] widget::AppStatusTaskStrip* statusStrip() const noexcept {
        return m_statusStrip;
    }

    /// Sets or clears current document/project name and page number display in title bar.
    void setProjectTitleInfo(const QString& projectName, int currentPage, int totalPages, bool isDirty = false);
    void clearProjectTitleInfo();

    /// Marks whether a project session is open. The editor menu bar in the title
    /// bar is only shown on the editor route while a project session is active.
    void setProjectSessionActive(bool active);

    /// Updates project processing menu availability from the current editor snapshot.
    void setEditorProcessingState(const screen::editor::EditorState& state);

    /// Wires the controller to the title bar and panel intents. Non-owning.
    void setNavigationController(navigation::NavigationController* controller);

    /// Call to force close the window bypassing interceptors
    void forceClose();

    /// Call to force navigation bypassing interceptors
    void forceNavigate(const QString& routeKey);

Q_SIGNALS:
    /// Emitted when the user attempts to close the window.
    /// If connected, receivers must call forceClose() later to actually close.
    void closeRequested();

    /// Emitted when the user attempts to navigate to a route.
    /// If connected, receivers must call forceNavigate(routeKey) later to actually navigate.
    void navigationInterceptRequested(const QString& routeKey);

    /// Title bar menu intents. The composition root wires these to the real
    /// use cases; the window only reports user intent.
    void importFilesRequested();
    void importFolderRequested();
    void exportProjectRequested();
    void closeProjectRequested();
    void processProjectRequested();
    void runDetectionRequested();
    void runOcrRequested();
    void runTranslationRequested();
    void runTextClearRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void restoreWindowGeometry();
    void saveWindowGeometry();
    void updateEditorMenuBarVisibility();

    application::IAppSettingsStore* m_settingsStore;         // non-owning
    navigation::NavigationController* m_controller{nullptr}; // non-owning
    widget::AppStatusTaskStrip* m_statusStrip{nullptr};
    fluent::textfields::Label* m_projectTitleLabel{nullptr};
    fluent::menus_toolbars::FluentMenuBar* m_editorMenuBar{nullptr};
    QAction* m_processProjectAction{nullptr};
    QAction* m_detectionAction{nullptr};
    QAction* m_ocrAction{nullptr};
    QAction* m_translationAction{nullptr};
    QAction* m_clearAction{nullptr};
    QString m_lastMainRoute{QStringLiteral("home")};
    bool m_forceClose{false};
    bool m_editorRouteActive{false};
    bool m_projectSessionActive{false};
};

} // namespace manga::ui::window
