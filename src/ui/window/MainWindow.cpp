#include "ui/window/MainWindow.h"

#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/window/TitleBar.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeySequence>
#include <QScreen>
#include <QShortcut>

namespace manga::ui::window {

namespace {
constexpr auto kKeyNormalGeometry = "ui/mainWindow/normalGeometry";
constexpr auto kKeyMaximized = "ui/mainWindow/maximized";
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kMinVisiblePx = 64;
} // namespace

MainWindow::MainWindow(application::IAppSettingsStore* settingsStore, QWidget* parent)
    : NavigationWindow(parent), m_settingsStore(settingsStore) {
    setMinimumSize(640, 480);
    resize(kDefaultWidth, kDefaultHeight);
    setWindowTitle(QStringLiteral("MangaTranslator"));
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        // The offscreen platform has no compositor; translucent backdrops are meaningless there.
        setBackdropEffect(fluent::windowing::BackdropEffect::Solid);
    }
    restoreWindowGeometry();
}

void MainWindow::setNavigationController(navigation::NavigationController* controller) {
    m_controller = controller;
    if (m_controller == nullptr) {
        return;
    }
    connect(this, &NavigationWindow::backRequested, m_controller, &navigation::NavigationController::goBack);
    connect(this, &NavigationWindow::navigationRequested, m_controller, &navigation::NavigationController::navigate);
    connect(m_controller, &navigation::NavigationController::canGoBackChanged, this, [this](bool canGoBack) {
        titleBar()->setBackButtonVisible(canGoBack);
    });
    titleBar()->setBackButtonVisible(m_controller->canGoBack());

    auto* backShortcut = new QShortcut(QKeySequence::Back, this);
    connect(backShortcut, &QShortcut::activated, m_controller, &navigation::NavigationController::goBack);

    auto* forwardShortcut = new QShortcut(QKeySequence::Forward, this);
    connect(forwardShortcut, &QShortcut::activated, m_controller, &navigation::NavigationController::goForward);
}

void MainWindow::restoreWindowGeometry() {
    if (m_settingsStore == nullptr) {
        return;
    }
    const auto maximized = m_settingsStore->readSetting(QString::fromLatin1(kKeyMaximized));
    const auto geometry = m_settingsStore->readSetting(QString::fromLatin1(kKeyNormalGeometry));
    if (maximized && maximized->toBool()) {
        setWindowState(windowState() | Qt::WindowMaximized);
        return;
    }
    if (geometry && !geometry->toByteArray().isEmpty() && restoreGeometry(geometry->toByteArray())) {
        // Clamp: at least 64x64 of the frame must land on some usable screen, otherwise
        // fall back to the centered default on the primary screen.
        const QRect frame = frameGeometry();
        for (const QScreen* screen : QGuiApplication::screens()) {
            const QRect overlap = screen->availableGeometry().intersected(frame);
            if (overlap.width() >= kMinVisiblePx && overlap.height() >= kMinVisiblePx) {
                return;
            }
        }
    }
    resize(kDefaultWidth, kDefaultHeight);
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - QRect(0, 0, kDefaultWidth, kDefaultHeight).center());
    }
}

void MainWindow::saveWindowGeometry() {
    if (m_settingsStore == nullptr) {
        return;
    }
    m_settingsStore->writeSetting(QString::fromLatin1(kKeyMaximized), isMaximized());
    if (!isMaximized()) {
        // While maximized keep the most recent normal geometry instead of the maximized rect.
        m_settingsStore->writeSetting(QString::fromLatin1(kKeyNormalGeometry), saveGeometry());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveWindowGeometry();
    NavigationWindow::closeEvent(event);
}

} // namespace manga::ui::window
