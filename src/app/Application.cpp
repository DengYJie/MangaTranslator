#include "app/Application.h"

#include "app/ApplicationContext.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/usecase/settings/AppSettingsUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationTypes.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/home/HomePage.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/import/ImportPage.h"
#include "ui/screen/import/ImportViewModel.h"
#include "ui/screen/settings/SettingsPage.h"
#include "ui/screen/settings/SettingsState.h"
#include "ui/screen/settings/SettingsViewModel.h"
#include "ui/widget/AppStatusTaskStrip.h"
#include "ui/window/MainWindow.h"

#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QStyleHints>
#include <QTimer>

namespace manga::app {

namespace {

void applyFluentTheme(application::AppTheme theme) {
    switch (theme) {
        case application::AppTheme::Light:
            fluent::FluentElement::setTheme(fluent::FluentElement::Light);
            break;
        case application::AppTheme::Dark:
            fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
            break;
        case application::AppTheme::System:
            fluent::FluentElement::setTheme(QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
                                                ? fluent::FluentElement::Dark
                                                : fluent::FluentElement::Light);
            break;
    }
}

} // namespace

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
    m_processingController.reset();
    m_mainWindow.reset();
    if (m_context) {
        m_context->shutdown();
    }
    m_context.reset();
    m_app.reset();
}

void Application::applyThemeFromSettings() {
    const application::LoadAppSettingsUseCase loadSettings(m_context->appSettingsStore());
    applyFluentTheme(loadSettings.execute().theme);
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

    auto* editorViewModel = new ui::screen::editor::EditorViewModel(m_context->thumbnailStore(),
                                                                    m_context->pageImageLoader(),
                                                                    m_context->operationRegistry(),
                                                                    m_context->projectRepository(),
                                                                    mainWindow);
    auto* editorPage = new ui::screen::editor::EditorPage(editorViewModel);
    m_processingController =
        std::make_unique<ui::screen::editor::EditorProcessingController>(editorViewModel,
                                                                         m_context->ocrEngine(),
                                                                         m_context->translator(),
                                                                         m_context->inpainter(),
                                                                         m_context->textLayouter(),
                                                                         m_context->derivedAssetStore(),
                                                                         m_context->operationRegistry(),
                                                                         nullptr);

    ui::navigation::NavigationItemDescriptor editorItem;
    editorItem.routeKey = QStringLiteral("editor");
    editorItem.text = QCoreApplication::translate("Application", "编辑器");
    editorItem.iconGlyph = QStringLiteral("\uE70F");
    mainWindow->addNavigationItem(editorItem, QStringLiteral("main"));
    mainWindow->registerRoute(QStringLiteral("editor"), editorPage, QStringLiteral("main"));

    auto* settingsViewModel = new ui::screen::settings::SettingsViewModel(
        m_context->appSettingsStore(), m_context->credentialOperations(), m_context->ocrModelValidator(), mainWindow);
    auto* generalSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::General);
    auto* ocrSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::Ocr);
    auto* translationSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::AiTranslation);
    auto* appearanceSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::Appearance);

    auto* settingsPanel = new ui::navigation::NavigationPanel(nullptr);
    mainWindow->registerSurface(QStringLiteral("settings"), settingsPanel);

    ui::navigation::NavigationItemDescriptor generalItem;
    generalItem.routeKey = QStringLiteral("settings/general");
    generalItem.text = QCoreApplication::translate("Application", "常规");
    generalItem.iconGlyph = Typography::Icons::Settings;
    mainWindow->addNavigationItem(generalItem, QStringLiteral("settings"));
    mainWindow->registerRoute(QStringLiteral("settings/general"), generalSettingsPage, QStringLiteral("settings"));

    ui::navigation::NavigationItemDescriptor ocrItem;
    ocrItem.routeKey = QStringLiteral("settings/ocr");
    ocrItem.text = QCoreApplication::translate("Application", "OCR");
    ocrItem.iconGlyph = Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular"));
    mainWindow->addNavigationItem(ocrItem, QStringLiteral("settings"));
    mainWindow->registerRoute(QStringLiteral("settings/ocr"), ocrSettingsPage, QStringLiteral("settings"));

    ui::navigation::NavigationItemDescriptor translationItem;
    translationItem.routeKey = QStringLiteral("settings/ai-translation");
    translationItem.text = QCoreApplication::translate("Application", "AI 翻译");
    translationItem.iconGlyph = Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular"));
    mainWindow->addNavigationItem(translationItem, QStringLiteral("settings"));
    mainWindow->registerRoute(
        QStringLiteral("settings/ai-translation"), translationSettingsPage, QStringLiteral("settings"));

    ui::navigation::NavigationItemDescriptor appearanceItem;
    appearanceItem.routeKey = QStringLiteral("settings/appearance");
    appearanceItem.text = QCoreApplication::translate("Application", "外观");
    appearanceItem.iconGlyph = Typography::Icons::Brush;
    mainWindow->addNavigationItem(appearanceItem, QStringLiteral("settings"));
    mainWindow->registerRoute(
        QStringLiteral("settings/appearance"), appearanceSettingsPage, QStringLiteral("settings"));

    // Main surface footer item for Settings
    ui::navigation::NavigationItemDescriptor settingsFooterItem;
    settingsFooterItem.routeKey = QStringLiteral("settings/general");
    settingsFooterItem.text = QCoreApplication::translate("Application", "设置");
    settingsFooterItem.iconGlyph = Typography::Icons::Settings;
    settingsFooterItem.position = ui::navigation::NavigationItemPosition::Bottom;
    mainWindow->addNavigationItem(settingsFooterItem, QStringLiteral("main"));

    // Theme instant preview wiring
    QObject::connect(settingsViewModel,
                     &ui::screen::settings::SettingsViewModel::themePreviewRequested,
                     mainWindow,
                     [](application::AppTheme theme) { applyFluentTheme(theme); });

    QObject::connect(QGuiApplication::styleHints(),
                     &QStyleHints::colorSchemeChanged,
                     mainWindow,
                     [settingsViewModel](Qt::ColorScheme) {
                         if (settingsViewModel->state().persisted.theme == application::AppTheme::System) {
                             applyFluentTheme(application::AppTheme::System);
                         }
                     });

    QObject::connect(controller,
                     &ui::navigation::NavigationController::routeChanged,
                     mainWindow,
                     [mainWindow](const QString& routeKey) {
                         if (auto* panel = mainWindow->activeNavigationPanel()) {
                             panel->setCurrentItem(routeKey);
                         }
                     });

    // --- Close/Navigation Interception Workflow ---
    auto pendingAction = std::make_shared<QString>(); // "" means window close, otherwise target route

    auto handleProjectClose = [mainWindow, controller, pendingAction]() {
        if (pendingAction->isEmpty()) {
            mainWindow->forceClose();
        } else {
            mainWindow->forceNavigate(*pendingAction);
        }
    };

    QObject::connect(mainWindow,
                     &ui::window::MainWindow::closeRequested,
                     mainWindow,
                     [editorViewModel, settingsViewModel, pendingAction, mainWindow]() {
                         *pendingAction = QString();
                         if (mainWindow->activeNavigationSurfaceId() == QLatin1String("settings") &&
                             settingsViewModel->state().fieldErrors.contains(QStringLiteral("persist"))) {
                             return;
                         }
                         editorViewModel->requestCloseProject();
                     });

    QObject::connect(
        mainWindow,
        &ui::window::MainWindow::navigationInterceptRequested,
        mainWindow,
        [editorViewModel, settingsViewModel, pendingAction, controller, mainWindow](const QString& routeKey) {
            *pendingAction = routeKey;
            if (routeKey.startsWith(QLatin1String("settings"))) {
                if (editorViewModel->hasSession() && editorViewModel->state().saveState.isDirty) {
                    editorViewModel->requestCloseProject();
                } else {
                    controller->navigate(routeKey);
                }
                return;
            }

            if (mainWindow->activeNavigationSurfaceId() == QLatin1String("settings") &&
                settingsViewModel->state().fieldErrors.contains(QStringLiteral("persist"))) {
                return;
            }

            editorViewModel->requestCloseProject();
        });

    QObject::connect(
        editorViewModel, &ui::screen::editor::EditorViewModel::projectClosed, mainWindow, handleProjectClose);

    QObject::connect(editorViewModel,
                     &ui::screen::editor::EditorViewModel::closeDecisionRequested,
                     mainWindow,
                     [editorViewModel, mainWindow, handleProjectClose](
                         const manga::ui::screen::editor::ProjectCloseSummary& summary) {
                         auto* dialog = new fluent::dialogs_flyouts::ContentDialog(mainWindow);
                         dialog->setTitle(summary.projectName);
                         dialog->setContent(new QLabel(summary.safeMessage));
                         dialog->setPrimaryButtonText(QCoreApplication::translate("Application", "保存并关闭"));
                         dialog->setSecondaryButtonText(QCoreApplication::translate("Application", "不保存"));
                         dialog->setCloseButtonText(QCoreApplication::translate("Application", "取消"));

                         QObject::connect(dialog,
                                          &fluent::dialogs_flyouts::ContentDialog::primaryButtonClicked,
                                          editorViewModel,
                                          [editorViewModel, mainWindow, handleProjectClose]() {
                                              // When saving finishes, EditorViewModel emits saveCompleted. We connect a
                                              // one-off lambda to catch it.
                                              auto saveConn = std::make_shared<QMetaObject::Connection>();
                                              auto failConn = std::make_shared<QMetaObject::Connection>();

                                              *saveConn = QObject::connect(
                                                  editorViewModel,
                                                  &ui::screen::editor::EditorViewModel::saveCompleted,
                                                  mainWindow,
                                                  [editorViewModel, handleProjectClose, saveConn, failConn]() {
                                                      QObject::disconnect(*saveConn);
                                                      QObject::disconnect(*failConn);
                                                      editorViewModel->confirmDiscardAndClose();
                                                  });

                                              *failConn =
                                                  QObject::connect(editorViewModel,
                                                                   &ui::screen::editor::EditorViewModel::saveFailed,
                                                                   mainWindow,
                                                                   [saveConn, failConn]() {
                                                                       QObject::disconnect(*saveConn);
                                                                       QObject::disconnect(*failConn);
                                                                   });

                                              editorViewModel->saveProject();
                                          });

                         QObject::connect(dialog,
                                          &fluent::dialogs_flyouts::ContentDialog::secondaryButtonClicked,
                                          editorViewModel,
                                          &ui::screen::editor::EditorViewModel::confirmDiscardAndClose);
                         QObject::connect(dialog,
                                          &fluent::dialogs_flyouts::ContentDialog::closeButtonClicked,
                                          editorViewModel,
                                          &ui::screen::editor::EditorViewModel::cancelClose);

                         dialog->show();
                     });
    // --- End Close/Navigation Interception Workflow ---

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
    QObject::connect(
        homePage, &ui::screen::home::HomePage::settingsRequested, controller, [controller](const QString& routeKey) {
            controller->navigate(routeKey.isEmpty() ? QStringLiteral("settings/general") : routeKey);
        });

    // --- AppStatusTaskStrip Wiring ---
    if (auto* statusStrip = mainWindow->statusStrip()) {
        QObject::connect(m_context->operationRegistry(),
                         &application::OperationRegistry::operationsChanged,
                         statusStrip,
                         &ui::widget::AppStatusTaskStrip::setOperations);

        QObject::connect(statusStrip,
                         &ui::widget::AppStatusTaskStrip::cancelRequested,
                         m_context->operationRegistry(),
                         &application::OperationRegistry::cancelOperation);

        QObject::connect(editorViewModel,
                         &ui::screen::editor::EditorViewModel::stateChanged,
                         statusStrip,
                         [statusStrip](const ui::screen::editor::EditorState& state) {
                             ui::widget::DocumentStatusSnapshot docStatus;
                             if (!state.projectId.isValid()) {
                                 docStatus.status = ui::widget::DocumentSaveStatus::NoDocument;
                             } else if (state.saveState.saveError.has_value()) {
                                 docStatus.status = ui::widget::DocumentSaveStatus::SaveFailed;
                                 docStatus.message = QCoreApplication::translate("Application", "保存失败");
                                 docStatus.failureDetail = *state.saveState.saveError;
                             } else if (state.saveState.isSaving) {
                                 docStatus.status = ui::widget::DocumentSaveStatus::Saving;
                                 docStatus.message = QCoreApplication::translate("Application", "正在保存");
                             } else if (state.saveState.isDirty) {
                                 docStatus.status = ui::widget::DocumentSaveStatus::Dirty;
                                 docStatus.message = QCoreApplication::translate("Application", "有未保存更改");
                             } else {
                                 docStatus.status = ui::widget::DocumentSaveStatus::Saved;
                                 docStatus.message = QCoreApplication::translate("Application", "所有更改已保存");
                             }
                             statusStrip->setDocumentStatus(docStatus);
                         });

        QObject::connect(
            editorViewModel, &ui::screen::editor::EditorViewModel::projectClosed, statusStrip, [statusStrip]() {
                ui::widget::DocumentStatusSnapshot docStatus;
                docStatus.status = ui::widget::DocumentSaveStatus::NoDocument;
                statusStrip->setDocumentStatus(docStatus);
            });

        QObject::connect(statusStrip,
                         &ui::widget::AppStatusTaskStrip::operationActivated,
                         mainWindow,
                         [editorViewModel, controller](const application::OperationId& /*id*/,
                                                       const domain::ProjectId& projectId,
                                                       const domain::PageId& pageId) {
                             if (editorViewModel->hasSession() && editorViewModel->projectAggregate() &&
                                 editorViewModel->projectAggregate()->id() == projectId) {
                                 controller->navigate(QStringLiteral("editor"));
                                 if (pageId.isValid()) {
                                     editorViewModel->selectPage(pageId);
                                 }
                             }
                         });

        QObject::connect(statusStrip,
                         &ui::widget::AppStatusTaskStrip::saveFailureDetailsRequested,
                         mainWindow,
                         [editorViewModel, controller]() {
                             if (editorViewModel->hasSession()) {
                                 controller->navigate(QStringLiteral("editor"));
                             }
                         });
    }

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
        QTimer::singleShot(0, m_app.get(), [app = m_app.get()]() { app->exit(0); });
    }
    return m_app->exec();
}

} // namespace manga::app
