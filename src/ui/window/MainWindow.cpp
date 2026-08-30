#include "ui/window/MainWindow.h"

#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationSurfaceHost.h"

#include <FluentQt/BasicInput.h>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
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

MainWindow::MainWindow(application::IAppSettingsStore* settingsStore,
                       navigation::NavigationSurfaceHost* host,
                       navigation::NavigationController* controller,
                       QWidget* parent)
    : fluent::windowing::Window(parent), m_settingsStore(settingsStore), m_controller(controller) {
    setMinimumSize(640, 480);
    resize(kDefaultWidth, kDefaultHeight);
    setWindowTitle(QStringLiteral("MangaTranslator"));
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        setBackdropEffect(fluent::windowing::BackdropEffect::Solid); // Offscreen has no compositor.
    }
    setContentWidget(host);
    buildTitleBar();
    restoreWindowGeometry();

    auto* backShortcut = new QShortcut(QKeySequence::Back, this);
    connect(backShortcut, &QShortcut::activated, this, [this]() { m_controller->goBack(); });
    connect(m_controller, &navigation::NavigationController::canGoBackChanged, this, [this](bool canGoBack) {
        m_backButton->setVisible(canGoBack);
    });
    m_backButton->setVisible(m_controller->canGoBack());
}

MainWindow::~MainWindow() {
    // The shell hosts but does not own the navigation surface; detach it so QObject
    // tree teardown never deletes a caller-owned host.
    setContentWidget(nullptr);
}

void MainWindow::buildTitleBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(4);
    m_backButton = new fluent::basicinput::Button(QString(), bar);
    m_backButton->setIconGlyph(QChar(0xE76B)); // Back glyph
    m_backButton->setAccessibleName(tr("返回"));
    m_backButton->setToolTip(tr("返回"));
    connect(m_backButton, &fluent::basicinput::Button::clicked, this, [this]() { m_controller->goBack(); });
    auto* title = new QLabel(QStringLiteral("MangaTranslator"), bar);
    layout->addWidget(m_backButton);
    layout->addWidget(title);
    layout->addStretch(1);
    titleBar()->setContentWidget(bar);
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
    if (geometry && geometry->toByteArray().size() > 0 && restoreGeometry(geometry->toByteArray())) {
        // Clamp: at least 64x64 of content must land inside some usable screen,
        // otherwise fall back to the primary-screen centered default.
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
    fluent::windowing::Window::closeEvent(event);
}

} // namespace manga::ui::window
