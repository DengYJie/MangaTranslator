#include "app/Application.h"

#include "app/ApplicationContext.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/usecase/settings/AppSettingsUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "domain/model/Uuid.h"
#include "network/translation/ProviderConfig.h"
#include "rendering/FontRegistry.h"
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
#include "ui/widget/EditorCommandBar.h"
#include "ui/window/MainWindow.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/FluentQt.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
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

Application::Application(int& argc, char** argv) : Application(argc, argv, nullptr) {}

Application::Application(int& argc, char** argv, std::unique_ptr<ApplicationContext> injectedContext) {
    // FluentQt constraint: High DPI must be prepared before QApplication exists.
    fluent::prepareHighDpiApplication();

    if (!QCoreApplication::instance()) {
        m_app = std::make_unique<QApplication>(argc, argv);
        m_app->setApplicationName(QStringLiteral("MangaTranslator"));
        m_app->setApplicationVersion(QStringLiteral("0.1.0"));
        m_app->setOrganizationName(QStringLiteral("MangaTranslator"));
    }

    if (!fluent::initializeResources()) {
        m_initFailed = true;
        return;
    }

    // Register and discover bundled fonts such as NotoSansCJK-VF.
    rendering::FontRegistry::bundledFontFamilies();

    const QIcon appIcon(QStringLiteral(":/icons/manga-translator-app.svg"));
    if (m_app) {
        m_app->setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());
        m_app->setWindowIcon(appIcon);
        m_smokeTest = m_app->arguments().contains(QStringLiteral("--smoke-test"));
    } else if (qApp) {
        qApp->setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());
        qApp->setWindowIcon(appIcon);
        m_smokeTest = qApp->arguments().contains(QStringLiteral("--smoke-test"));
    }

    m_context = injectedContext ? std::move(injectedContext) : std::make_unique<ApplicationContext>();
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
    if (!m_context || !m_context->appSettingsStore()) {
        return;
    }
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
    auto* homePage = new ui::screen::home::HomePage(homeViewModel, mainWindow);

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
    editorViewModel->setOcrAvailability(m_context->isOcrModelAvailable(), m_context->ocrModelUnavailableReason());
    editorViewModel->setInpaintAvailability(m_context->isInpaintModelAvailable(),
                                            m_context->inpaintModelUnavailableReason());
    editorViewModel->setExportDependencies(m_context->comicExporter(), m_context->derivedAssetStore());
    editorViewModel->setAppendPagesWorkflow(m_context->appendPagesWorkflow());
    auto* editorPage = new ui::screen::editor::EditorPage(editorViewModel, mainWindow);
    QObject::connect(editorPage,
                     &ui::screen::editor::EditorPage::chooseExportPathRequested,
                     editorPage->commandBarForTest(),
                     &ui::widget::EditorCommandBar::chooseExportPathRequested);
    QObject::connect(editorPage->commandBarForTest(),
                     &ui::widget::EditorCommandBar::chooseExportPathRequested,
                     mainWindow,
                     [editorViewModel, mainWindow]() {
                         const QString path = QFileDialog::getExistingDirectory(
                             mainWindow, QCoreApplication::translate("Application", "选择导出目录"));
                         if (!path.isEmpty()) {
                             editorViewModel->retryExportInDirectory(path);
                         }
                     });
    m_processingController =
        std::make_unique<ui::screen::editor::EditorProcessingController>(editorViewModel,
                                                                         m_context->ocrEngine(),
                                                                         m_context->translator(),
                                                                         m_context->inpainter(),
                                                                         m_context->pageInpainter(),
                                                                         m_context->textLayouter(),
                                                                         m_context->derivedAssetStore(),
                                                                         m_context->operationRegistry(),
                                                                         m_context->textDetector(),
                                                                         m_context->batchPagesWorkflow(),
                                                                         nullptr);
    // Auto-adopts each proposal as soon as it is ready, so no manual accept is needed. A proposal
    // with no applicable change, for example one whose regions were skipped because they hold
    // manual content, is discarded instead because adopting it would add an empty undo entry.
    auto* processingController = m_processingController.get();
    QObject::connect(processingController,
                     &ui::screen::editor::EditorProcessingController::proposalReady,
                     processingController,
                     [processingController](const domain::PageId& pageId) {
                         const auto* prepared = processingController->preparedAdoption(pageId);
                         if (prepared == nullptr) {
                             return;
                         }
                         if (!prepared->changes.isEmpty() || prepared->clearAssetChange) {
                             processingController->adoptProposal(pageId);
                         } else {
                             processingController->rejectProposal(pageId);
                         }
                     });
    editorViewModel->setDetectionAvailability(m_context->isDetectionModelAvailable(),
                                              m_context->detectionModelUnavailableReason());

    editorViewModel->observe(mainWindow, [mainWindow](const ui::screen::editor::EditorState& state) {
        const bool sessionActive = state.projectId.isValid() && !state.projectName.isEmpty();
        mainWindow->setProjectSessionActive(sessionActive);
        if (auto* panel = mainWindow->mainNavigationPanel()) {
            panel->setItemEnabled(QStringLiteral("editor"), sessionActive);
        }
        if (!sessionActive) {
            mainWindow->clearProjectTitleInfo();
            return;
        }
        int totalPages = state.pages.size();
        int currentPage = 0;
        if (state.currentPageId.isValid()) {
            for (int i = 0; i < state.pages.size(); ++i) {
                if (state.pages.at(i).id == state.currentPageId) {
                    currentPage = i + 1;
                    break;
                }
            }
        }
        mainWindow->setProjectTitleInfo(state.projectName, currentPage, totalPages, state.saveState.isDirty);
    });

    ui::navigation::NavigationItemDescriptor editorItem;
    editorItem.routeKey = QStringLiteral("editor");
    editorItem.text = QCoreApplication::translate("Application", "编辑器");
    editorItem.iconGlyph = QStringLiteral("\uE70F");
    mainWindow->addNavigationItem(editorItem, QStringLiteral("main"));
    mainWindow->registerRoute(QStringLiteral("editor"), editorPage, QStringLiteral("main"));
    // The observe callback above replays before the editor item is registered,
    // so the initial enabled state is applied here explicitly.
    if (auto* panel = mainWindow->mainNavigationPanel()) {
        panel->setItemEnabled(editorItem.routeKey, editorViewModel->hasSession());
    }

    auto* settingsViewModel = new ui::screen::settings::SettingsViewModel(m_context->appSettingsStore(),
                                                                          m_context->credentialOperations(),
                                                                          m_context->ocrModelValidator(),
                                                                          m_context->inpaintModelValidator(),
                                                                          m_context->detectionModelValidator(),
                                                                          mainWindow);
    settingsViewModel->observe(mainWindow, [this, editorViewModel](const ui::screen::settings::SettingsState& state) {
        network::ProviderConfig provider;
        provider.providerId = QStringLiteral("openai-compatible");
        provider.endpoint = QUrl(state.persisted.endpoint);
        provider.model = state.persisted.modelName;
        provider.timeoutMs = state.persisted.timeoutSeconds * 1000;
        m_context->updateProviderConfig(std::make_shared<const network::ProviderConfig>(std::move(provider)));

        const bool configured = !state.persisted.endpoint.trimmed().isEmpty() &&
                                !state.persisted.modelName.trimmed().isEmpty() &&
                                !state.fieldErrors.contains(QStringLiteral("endpoint")) &&
                                !state.fieldErrors.contains(QStringLiteral("modelName"));
        const bool credentialsSatisfied =
            state.credentialStatus == application::CredentialState::Configured ||
            network::isLoopbackHost(QUrl(state.persisted.endpoint.trimmed(), QUrl::TolerantMode).host());
        const bool translationAvailable = configured && credentialsSatisfied;
        QString reason;
        if (!translationAvailable) {
            reason = QCoreApplication::translate("Application", "请在设置中补全有效的 API 端点、模型名称和访问密钥。");
        }

        m_context->reconfigureInpaintModel(state.persisted.inpaintModelDirectory);
        if (m_processingController != nullptr) {
            m_processingController->setPageInpainter(m_context->pageInpainter());
        }
        editorViewModel->setInpaintAvailability(m_context->isInpaintModelAvailable(),
                                                m_context->inpaintModelUnavailableReason());

        m_context->reconfigureDetectionModel(state.persisted.detectionModelDirectory);
        if (m_processingController != nullptr) {
            m_processingController->setTextDetector(m_context->textDetector());
        }
        editorViewModel->setDetectionAvailability(m_context->isDetectionModelAvailable(),
                                                  m_context->detectionModelUnavailableReason());
    });
    auto* modelsSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::Models);
    auto* translationSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::AiTranslation);
    auto* appearanceSettingsPage = ui::screen::settings::SettingsPage::createLazyPage(
        settingsViewModel, ui::screen::settings::SettingsCategory::Appearance);

    auto* settingsPanel = new ui::navigation::NavigationPanel(nullptr);
    mainWindow->registerSurface(QStringLiteral("settings"), settingsPanel);

    ui::navigation::NavigationItemDescriptor modelsItem;
    modelsItem.routeKey = QStringLiteral("settings/models");
    modelsItem.text = QCoreApplication::translate("Application", "模型配置");
    modelsItem.iconGlyph = Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular"));
    mainWindow->addNavigationItem(modelsItem, QStringLiteral("settings"));
    mainWindow->registerRoute(QStringLiteral("settings/models"), modelsSettingsPage, QStringLiteral("settings"));

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
    settingsFooterItem.routeKey = QStringLiteral("settings/models");
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
    auto pendingProjectPath = std::make_shared<QString>();

    auto handleProjectClose = [this, homeViewModel, mainWindow, controller, pendingAction, pendingProjectPath]() {
        mainWindow->clearProjectTitleInfo();
        const QString projectPath = *pendingProjectPath;
        if (!projectPath.isEmpty()) {
            pendingProjectPath->clear();
            QMetaObject::invokeMethod(
                mainWindow,
                [homeViewModel, projectPath]() { homeViewModel->openProjectPath(projectPath); },
                Qt::QueuedConnection);
            return;
        }

        const QString action = *pendingAction;
        QMetaObject::invokeMethod(
            mainWindow,
            [this, mainWindow, controller, action]() {
                if (action.isEmpty()) {
                    mainWindow->forceClose();
                    if (m_context) {
                        m_context->shutdown();
                    }
                    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
                        app->quit();
                    }
                } else {
                    mainWindow->forceNavigate(action);
                }
            },
            Qt::QueuedConnection);
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

    QObject::connect(homeViewModel,
                     &ui::screen::home::HomeViewModel::projectOpenRequested,
                     mainWindow,
                     [homeViewModel, editorViewModel, controller, pendingProjectPath](const QString& path) {
                         const auto normalizePath = [](const QString& value) {
                             const QFileInfo info(value);
                             const QString canonical = info.canonicalFilePath();
                             return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
                         };
                         if (editorViewModel->hasSession() &&
                             normalizePath(path).compare(normalizePath(editorViewModel->projectRootPath()),
                                                         Qt::CaseInsensitive) == 0) {
                             controller->navigate(QStringLiteral("editor"));
                             return;
                         }

                         *pendingProjectPath = path;
                         if (editorViewModel->hasSession() && editorViewModel->state().saveState.isDirty) {
                             editorViewModel->requestCloseProject();
                         } else {
                             pendingProjectPath->clear();
                             homeViewModel->openProjectPath(path);
                         }
                     });

    QObject::connect(mainWindow,
                     &ui::window::MainWindow::navigationInterceptRequested,
                     mainWindow,
                     [settingsViewModel, pendingAction, controller, mainWindow](const QString& routeKey) {
                         *pendingAction = routeKey;
                         if (routeKey.startsWith(QLatin1String("settings"))) {
                             controller->navigate(routeKey);
                             return;
                         }

                         if (mainWindow->activeNavigationSurfaceId() == QLatin1String("settings") &&
                             settingsViewModel->state().fieldErrors.contains(QStringLiteral("persist"))) {
                             return;
                         }

                         controller->navigate(routeKey);
                     });

    QObject::connect(
        editorViewModel, &ui::screen::editor::EditorViewModel::projectClosed, mainWindow, handleProjectClose);

    QObject::connect(
        editorViewModel,
        &ui::screen::editor::EditorViewModel::closeDecisionRequested,
        mainWindow,
        [editorViewModel, mainWindow, pendingProjectPath](
            const manga::ui::screen::editor::ProjectCloseSummary& summary) {
            auto* dialog = new fluent::dialogs_flyouts::ContentDialog(mainWindow);
            dialog->setObjectName(QStringLiteral("projectCloseDialog"));
            dialog->setTitle(summary.projectName);
            auto* message = new fluent::textfields::Label(summary.safeMessage);
            message->setObjectName(QStringLiteral("projectCloseMessage"));
            message->setFluentTypography(Typography::FontRole::Body);
            message->setWordWrap(true);
            message->setTextColorRole(fluent::textfields::Label::TextColorRole::Primary);
            dialog->setContent(message);
            dialog->setPrimaryButtonText(QCoreApplication::translate("Application", "保存并关闭"));
            dialog->setSecondaryButtonText(QCoreApplication::translate("Application", "不保存"));
            dialog->setCloseButtonText(QCoreApplication::translate("Application", "取消"));
            dialog->setDefaultButton(fluent::dialogs_flyouts::ContentDialog::Primary);
            dialog->setWindowModality(Qt::ApplicationModal);
            dialog->resize(480, 240);

            const auto buttons = dialog->findChildren<fluent::basicinput::Button*>();
            if (!buttons.isEmpty()) {
                auto* buttonBar = buttons.constFirst()->parentWidget();
                auto* buttonLayout = buttonBar != nullptr ? qobject_cast<QHBoxLayout*>(buttonBar->layout()) : nullptr;
                if (buttonLayout != nullptr) {
                    for (int index = 0; index < buttons.size(); ++index) {
                        auto* button = buttons.at(index);
                        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                        button->setMinimumWidth(qMax(96, button->sizeHint().width() + 32));
                        buttonLayout->setStretch(index, 0);
                    }
                    buttonLayout->addStretch(1);
                }
            }

            QObject::connect(dialog,
                             &fluent::dialogs_flyouts::ContentDialog::primaryButtonClicked,
                             editorViewModel,
                             [editorViewModel, mainWindow, pendingProjectPath]() {
                                 // When saving finishes, EditorViewModel emits saveCompleted. We connect a
                                 // one-off lambda to catch it.
                                 auto saveConn = std::make_shared<QMetaObject::Connection>();
                                 auto failConn = std::make_shared<QMetaObject::Connection>();

                                 *saveConn = QObject::connect(editorViewModel,
                                                              &ui::screen::editor::EditorViewModel::saveCompleted,
                                                              mainWindow,
                                                              [editorViewModel, saveConn, failConn]() {
                                                                  QObject::disconnect(*saveConn);
                                                                  QObject::disconnect(*failConn);
                                                                  editorViewModel->confirmDiscardAndClose();
                                                              });

                                 *failConn =
                                     QObject::connect(editorViewModel,
                                                      &ui::screen::editor::EditorViewModel::saveFailed,
                                                      mainWindow,
                                                      [saveConn, failConn, pendingProjectPath, editorViewModel]() {
                                                          QObject::disconnect(*saveConn);
                                                          QObject::disconnect(*failConn);
                                                          pendingProjectPath->clear();
                                                          editorViewModel->cancelClose();
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
                             [editorViewModel, pendingProjectPath]() {
                                 pendingProjectPath->clear();
                                 editorViewModel->cancelClose();
                             });

            QObject::connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);

            dialog->open();
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
    QObject::connect(editorPage,
                     &ui::screen::editor::EditorPage::settingsRequested,
                     controller,
                     [controller](const QString& routeKey) { controller->navigate(routeKey); });
    QObject::connect(m_context->operationRegistry(),
                     &application::OperationRegistry::operationsChanged,
                     editorPage,
                     &ui::screen::editor::EditorPage::setOperations);
    QObject::connect(editorPage,
                     &ui::screen::editor::EditorPage::cancelOperationRequested,
                     m_context->operationRegistry(),
                     &application::OperationRegistry::cancelOperation);

    auto* importViewModel = new ui::screen::import_::ImportViewModel(
        m_context->projectRepository(), m_context->comicImporter(), m_context->operationRegistry(), mainWindow);
    auto* importPage = new ui::screen::import_::ImportPage(importViewModel, mainWindow);

    mainWindow->registerRoute(QStringLiteral("import"), importPage, QStringLiteral("main"));

    QObject::connect(
        mainWindow, &ui::window::MainWindow::importFilesRequested, mainWindow, [editorViewModel, mainWindow]() {
            const auto paths = QFileDialog::getOpenFileNames(
                mainWindow,
                QCoreApplication::translate("Application", "选择图片文件"),
                QString(),
                QCoreApplication::translate("Application", "图片文件 (*.jpg *.jpeg *.png *.webp)"));
            if (!paths.isEmpty()) {
                editorViewModel->importPagesIntoProject(paths);
            }
        });
    QObject::connect(
        mainWindow, &ui::window::MainWindow::importFolderRequested, mainWindow, [editorViewModel, mainWindow]() {
            const QString path = QFileDialog::getExistingDirectory(
                mainWindow, QCoreApplication::translate("Application", "选择图片文件夹"));
            if (!path.isEmpty()) {
                editorViewModel->importPagesIntoProject({path});
            }
        });
    QObject::connect(
        mainWindow, &ui::window::MainWindow::exportProjectRequested, mainWindow, [editorViewModel, mainWindow]() {
            const QString path = QFileDialog::getExistingDirectory(
                mainWindow, QCoreApplication::translate("Application", "选择导出目录"));
            if (!path.isEmpty()) {
                editorViewModel->exportPages(application::ExportScope::AllPages, path);
            }
        });
    QObject::connect(
        mainWindow, &ui::window::MainWindow::closeProjectRequested, mainWindow, [editorViewModel, pendingAction]() {
            // Reuse the close-interception workflow, landing on the home route
            // instead of closing the window.
            *pendingAction = QStringLiteral("home");
            editorViewModel->requestCloseProject();
        });

    const auto allProjectPageIds = [editorViewModel]() {
        QVector<domain::PageId> pageIds;
        const auto& pages = editorViewModel->state().pages;
        pageIds.reserve(pages.size());
        for (const auto& page : pages) {
            pageIds.append(page.id);
        }
        return pageIds;
    };
    QObject::connect(mainWindow,
                     &ui::window::MainWindow::processProjectRequested,
                     mainWindow,
                     [processingController, allProjectPageIds]() {
                         processingController->batchSmartTranslatePages(allProjectPageIds());
                     });
    QObject::connect(
        mainWindow,
        &ui::window::MainWindow::runDetectionRequested,
        mainWindow,
        [processingController, allProjectPageIds]() { processingController->batchDetectPages(allProjectPageIds()); });
    QObject::connect(
        mainWindow, &ui::window::MainWindow::runOcrRequested, mainWindow, [processingController, allProjectPageIds]() {
            processingController->batchOcrPages(allProjectPageIds());
        });
    QObject::connect(mainWindow,
                     &ui::window::MainWindow::runTranslationRequested,
                     mainWindow,
                     [processingController, allProjectPageIds]() {
                         processingController->batchTranslatePages(allProjectPageIds());
                     });
    QObject::connect(
        mainWindow,
        &ui::window::MainWindow::runTextClearRequested,
        mainWindow,
        [processingController, allProjectPageIds]() { processingController->batchClearPages(allProjectPageIds()); });

    QObject::connect(importViewModel,
                     &ui::screen::import_::ImportViewModel::projectImported,
                     homeViewModel,
                     [this, homeViewModel](const application::OpenedProjectSession& session) {
                         m_context->recentProjectsStore()->addRecentProject(
                             {QString::fromStdString(session.project.name()),
                              session.projectRootPath,
                              QDateTime::currentMSecsSinceEpoch()});
                         homeViewModel->refresh();
                     });
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
    auto* appInstance = m_app ? m_app.get() : qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!appInstance) {
        return EXIT_SUCCESS;
    }
    const auto args = appInstance->arguments();
    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        QTimer::singleShot(500, appInstance, [this, path]() {
            m_mainWindow->grab().save(path);
            QCoreApplication::quit();
        });
    }

    if (m_smokeTest) {
        QTimer::singleShot(0, appInstance, [app = appInstance]() { app->exit(0); });
    }
    return appInstance->exec();
}

} // namespace manga::app
