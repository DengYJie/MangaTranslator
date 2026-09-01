#pragma once

#include <memory>

class QApplication;

namespace manga::app {
class ApplicationContext;
}

namespace manga::ui::window {
class MainWindow;
}
namespace manga::ui::screen::editor {
class EditorProcessingController;
}

namespace manga::app {

/// Process lifetime owner: QApplication, composition root, main window.
class Application {
public:
    Application(int& argc, char** argv);
    Application(int& argc, char** argv, std::unique_ptr<ApplicationContext> injectedContext);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Returns the process exit code.
    int run();

    [[nodiscard]] ApplicationContext* context() const noexcept {
        return m_context.get();
    }
    [[nodiscard]] ui::window::MainWindow* mainWindow() const noexcept {
        return m_mainWindow.get();
    }

private:
    void applyThemeFromSettings();

    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<ApplicationContext> m_context;
    std::unique_ptr<ui::window::MainWindow> m_mainWindow;
    std::unique_ptr<ui::screen::editor::EditorProcessingController> m_processingController;
    bool m_smokeTest{false};
    bool m_initFailed{false};
};

} // namespace manga::app
