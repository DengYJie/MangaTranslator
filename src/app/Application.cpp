#include "app/Application.h"

#include "app/ApplicationContext.h"
#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationTypes.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/home/HomePage.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/import/ImportPage.h"
#include "ui/screen/import/ImportViewModel.h"
#include "ui/window/MainWindow.h"

#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QTimer>

namespace manga::app {

Application::Application(int& argc, char** argv) {
    // FluentQt constraint: High DPI must be prepared before QApplication exists.
    fluent::prepareHighDpiApplication();

    m_app = std::make_unique<QApplication>(argc, argv);
    m_app->setApplicationName(QStringLiteral("MangaTranslator"));
    m_app->setApplicationVersion(QStringLiteral("0.1.0"));
    m_app->setOrganizationName(QStringLiteral("MangaTranslator"));

    if (!fluent::initializeResources()) {
        m_initFailed = true;
        return;
    }
    m_app->setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());

    m_smokeTest = m_app->arguments().contains(QStringLiteral("--smoke-test"));

    m_context = std::make_unique<ApplicationContext>();
    // Theme is applied before any widget is constructed (window is created in run()).
    applyThemeFromSettings();
}

Application::~Application() {
    m_mainWindow.reset();
    if (m_context) {
        m_context->shutdown();
    }
    m_context.reset();
    m_app.reset();
}

void Application::applyThemeFromSettings() {
    const auto value = m_context->appSettingsStore()->readSetting(QStringLiteral("ui/theme"));
    const QString theme = value ? value->toString() : QStringLiteral("system");
    if (theme == QLatin1String("light")) {
        fluent::FluentElement::setTheme(fluent::FluentElement::Light);
    } else if (theme == QLatin1String("dark")) {
        fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
    }
    // "system" leaves the FluentQt default; following OS changes arrives with the settings page.
}

int Application::run() {
    if (m_initFailed) {
        return EXIT_FAILURE;
    }

    auto* mainWindow = new ui::window::MainWindow(m_context->appSettingsStore());
    auto* controller = new ui::navigation::NavigationController(mainWindow, mainWindow);
    mainWindow->setNavigationController(controller);

    auto* homeViewModel = new ui::screen::home::HomeViewModel(
        m_context->recentProjectsStore(), m_context->projectRepository(), m_context->operationRegistry(), mainWindow);
    auto* homePage = new ui::screen::home::HomePage(homeViewModel);

    ui::navigation::NavigationItemDescriptor homeItem;
    homeItem.routeKey = QStringLiteral("home");
    homeItem.text = QCoreApplication::translate("Application", "首页");
    homeItem.iconGlyph = QStringLiteral("\uE80F");
    mainWindow->addNavigationItem(homeItem, QStringLiteral("main"));
    mainWindow->registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main"));

    auto* editorViewModel = new ui::screen::editor::EditorViewModel(
        m_context->thumbnailStore(), m_context->pageImageLoader(), m_context->operationRegistry(), mainWindow);
    auto* editorPage = new ui::screen::editor::EditorPage(editorViewModel);

    ui::navigation::NavigationItemDescriptor editorItem;
    editorItem.routeKey = QStringLiteral("editor");
    editorItem.text = QCoreApplication::translate("Application", "编辑器");
    editorItem.iconGlyph = QStringLiteral("\uE70F");
    mainWindow->addNavigationItem(editorItem, QStringLiteral("main"));
    mainWindow->registerRoute(QStringLiteral("editor"), editorPage, QStringLiteral("main"));

    QObject::connect(homeViewModel,
                     &ui::screen::home::HomeViewModel::projectOpened,
                     editorViewModel,
                     &ui::screen::editor::EditorViewModel::openProjectSession);
    QObject::connect(
        homeViewModel,
        &ui::screen::home::HomeViewModel::projectOpened,
        controller,
        [controller](const application::OpenedProjectSession&) { controller->navigate(QStringLiteral("editor")); });
    QObject::connect(editorPage, &ui::screen::editor::EditorPage::openProjectRequested, controller, [controller]() {
        controller->navigate(QStringLiteral("home"));
    });
    QObject::connect(editorPage, &ui::screen::editor::EditorPage::importPagesRequested, controller, [controller]() {
        controller->navigate(QStringLiteral("import"));
    });

    auto* importViewModel = new ui::screen::import_::ImportViewModel(
        m_context->projectRepository(), m_context->comicImporter(), m_context->operationRegistry());
    auto* importPage = new ui::screen::import_::ImportPage(importViewModel);

    mainWindow->registerRoute(QStringLiteral("import"), importPage, QStringLiteral("main"));

    QObject::connect(importViewModel,
                     &ui::screen::import_::ImportViewModel::projectImported,
                     editorViewModel,
                     &ui::screen::editor::EditorViewModel::openProjectSession);
    QObject::connect(
        importViewModel,
        &ui::screen::import_::ImportViewModel::projectImported,
        controller,
        [controller](const application::OpenedProjectSession&) { controller->navigate(QStringLiteral("editor")); });
    QObject::connect(importViewModel,
                     &ui::screen::import_::ImportViewModel::cancelRequested,
                     controller,
                     [controller]() { controller->navigate(QStringLiteral("home")); });
    QObject::connect(homePage, &ui::screen::home::HomePage::importRequested, controller, [controller]() {
        controller->navigate(QStringLiteral("import"));
    });

    m_mainWindow = std::unique_ptr<ui::window::MainWindow>(mainWindow);
    controller->navigate(QStringLiteral("home"));
    m_mainWindow->show();

    // Debug aid: --screenshot <path> grabs the settled window and quits.
    const auto args = m_app->arguments();
    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        QTimer::singleShot(500, m_app.get(), [this, path]() {
            m_mainWindow->grab().save(path);
            QCoreApplication::quit();
        });
    }

    if (m_smokeTest) {
        QTimer::singleShot(0, m_app.get(), &QCoreApplication::quit);
    }
    return m_app->exec();
}

} // namespace manga::app
