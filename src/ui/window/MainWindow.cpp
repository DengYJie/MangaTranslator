#include "ui/window/MainWindow.h"

#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/widget/AppStatusTaskStrip.h"
#include "ui/window/TitleBar.h"

#include <FluentQt/MenusToolbars.h>
#include <FluentQt/TextFields.h>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMetaMethod>
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

    const QIcon appIcon(QStringLiteral(":/icons/manga-translator-app.svg"));
    setWindowIcon(appIcon);

    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        // The offscreen platform has no compositor; translucent backdrops are meaningless there.
        setBackdropEffect(fluent::windowing::BackdropEffect::Solid);
    }

    m_projectTitleLabel = new fluent::textfields::Label(this);
    m_projectTitleLabel->setObjectName(QStringLiteral("mainWindowProjectTitleLabel"));
    m_projectTitleLabel->setFluentTypography(Typography::FontRole::Caption);
    m_projectTitleLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_projectTitleLabel->setAlignment(Qt::AlignCenter);
    m_projectTitleLabel->hide();

    if (titleBar()) {
        titleBar()->setIcon(appIcon);
        titleBar()->setContentWidget(m_projectTitleLabel, TitleBar::ContentAlignment::Center);
    }

    m_editorMenuBar = new fluent::menus_toolbars::FluentMenuBar(this);
    m_editorMenuBar->setObjectName(QStringLiteral("mainWindowEditorMenuBar"));
    m_editorMenuBar->setBackgroundVisible(false);
    m_editorMenuBar->setFontStyle(Typography::FontRole::Caption);

    // 1. File Menu ("文件")
    auto* fileMenu = new fluent::menus_toolbars::FluentMenu(tr("文件"), m_editorMenuBar);

    // 导入页面 (Submenu: 文件, 文件夹)
    auto* importMenu = new fluent::menus_toolbars::FluentMenu(tr("导入页面"), fileMenu);
    importMenu->addAction(tr("文件..."));
    importMenu->addAction(tr("文件夹..."));
    fileMenu->addMenu(importMenu);

    // 导出
    fileMenu->addAction(tr("导出..."));

    // 分割线
    fileMenu->addSeparator();

    // 关闭项目
    fileMenu->addAction(tr("关闭项目"));

    m_editorMenuBar->addMenu(fileMenu);

    // 2. Process Menu ("处理")
    auto* processMenu = new fluent::menus_toolbars::FluentMenu(tr("处理"), m_editorMenuBar);

    // 处理项目
    processMenu->addAction(tr("处理项目"));

    // 分割线
    processMenu->addSeparator();

    // 运行检测
    processMenu->addAction(tr("运行检测"));

    // 运行 OCR
    processMenu->addAction(tr("运行 OCR"));

    // 运行翻译
    processMenu->addAction(tr("运行翻译"));

    // 运行修补
    processMenu->addAction(tr("运行修补"));

    m_editorMenuBar->addMenu(processMenu);

    m_editorMenuBar->hide();
    if (titleBar()) {
        titleBar()->setMenuBar(m_editorMenuBar);
    }

    m_statusStrip = new widget::AppStatusTaskStrip(this);
    layout()->addWidget(m_statusStrip);

    restoreWindowGeometry();
}

void MainWindow::setNavigationController(navigation::NavigationController* controller) {
    m_controller = controller;
    if (m_controller == nullptr) {
        return;
    }
    connect(this, &NavigationWindow::backRequested, this, [this]() {
        if (m_controller == nullptr) {
            return;
        }
        const auto cur = m_controller->currentRoute();
        if (cur && cur->startsWith(QLatin1String("settings"))) {
            // 在设置界面点击返回：直接回到离开主 surface 前记录的页面
            const QString target = m_lastMainRoute.isEmpty() ? QStringLiteral("home") : m_lastMainRoute;
            m_controller->navigate(target);
        } else {
            m_controller->goBack();
        }
    });
    connect(this, &NavigationWindow::navigationRequested, this, [this](const QString& routeKey) {
        if (isSignalConnected(QMetaMethod::fromSignal(&MainWindow::navigationInterceptRequested))) {
            Q_EMIT navigationInterceptRequested(routeKey);
        } else {
            m_controller->navigate(routeKey);
        }
    });
    connect(m_controller, &navigation::NavigationController::routeChanged, this, [this](const QString& routeKey) {
        const bool isSettings = routeKey.startsWith(QLatin1String("settings"));
        const bool isEditor = (routeKey == QLatin1String("editor"));

        if (!isSettings) {
            m_lastMainRoute = routeKey;
        }

        if (titleBar()) {
            titleBar()->setBackButtonVisible(isSettings);
            titleBar()->updateLayout();
        }

        if (m_editorMenuBar) {
            m_editorMenuBar->setVisible(isEditor);
            if (titleBar()) {
                titleBar()->updateLayout();
            }
        }
    });

    const auto cur = m_controller->currentRoute();
    const bool isSettings = cur && cur->startsWith(QLatin1String("settings"));
    const bool isEditor = cur && (*cur == QLatin1String("editor"));
    if (!isSettings && cur) {
        m_lastMainRoute = *cur;
    }
    if (titleBar()) {
        titleBar()->setBackButtonVisible(isSettings);
    }
    if (m_editorMenuBar) {
        m_editorMenuBar->setVisible(isEditor);
    }

    auto* backShortcut = new QShortcut(QKeySequence::Back, this);
    connect(backShortcut, &QShortcut::activated, this, [this]() {
        if (m_controller == nullptr) {
            return;
        }
        const auto curRoute = m_controller->currentRoute();
        if (curRoute && curRoute->startsWith(QLatin1String("settings"))) {
            const QString target = m_lastMainRoute.isEmpty() ? QStringLiteral("home") : m_lastMainRoute;
            m_controller->navigate(target);
        } else {
            m_controller->goBack();
        }
    });

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

void MainWindow::forceClose() {
    m_forceClose = true;
    close();
}

void MainWindow::forceNavigate(const QString& routeKey) {
    if (m_controller) {
        m_controller->navigate(routeKey);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_forceClose && isSignalConnected(QMetaMethod::fromSignal(&MainWindow::closeRequested))) {
        event->ignore();
        Q_EMIT closeRequested();
        return;
    }

    saveWindowGeometry();
    NavigationWindow::closeEvent(event);
}

void MainWindow::setProjectTitleInfo(const QString& projectName, int currentPage, int totalPages, bool isDirty) {
    if (!m_projectTitleLabel) {
        return;
    }
    if (projectName.isEmpty()) {
        clearProjectTitleInfo();
        return;
    }
    QString text;
    if (totalPages > 0 && currentPage > 0) {
        text = QStringLiteral("%1%2 · %3 / %4")
                   .arg(projectName)
                   .arg(isDirty ? QStringLiteral("*") : QString())
                   .arg(currentPage)
                   .arg(totalPages);
    } else {
        text = QStringLiteral("%1%2").arg(projectName).arg(isDirty ? QStringLiteral("*") : QString());
    }
    m_projectTitleLabel->setText(text);
    m_projectTitleLabel->adjustSize();
    m_projectTitleLabel->show();
    if (titleBar()) {
        titleBar()->updateLayout();
    }
}

void MainWindow::clearProjectTitleInfo() {
    if (!m_projectTitleLabel) {
        return;
    }
    m_projectTitleLabel->clear();
    m_projectTitleLabel->hide();
    if (titleBar()) {
        titleBar()->updateLayout();
    }
}

} // namespace manga::ui::window
