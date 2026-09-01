#include "FakeOcrEngine.h"
#include "FakeTransport.h"
#include "app/Application.h"
#include "app/ApplicationContext.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IComicExporter.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/ICredentialStore.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IInpainter.h"
#include "application/ports/IOcrEngine.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/ports/ITextLayouter.h"
#include "application/ports/IThumbnailStore.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/TextRegion.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/import/ImportViewModel.h"
#include "ui/window/MainWindow.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/TextFields.h>
#include <QCoreApplication>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

using namespace manga::application;

class DestructionTrackingOcrEngine final : public IOcrEngine {
public:
    DestructionTrackingOcrEngine(bool* destroyedFlag, bool* completedBeforeDestruction)
        : m_destroyedFlag(destroyedFlag), m_completedBeforeDestruction(completedBeforeDestruction) {}

    ~DestructionTrackingOcrEngine() override {
        if (m_destroyedFlag) {
            *m_destroyedFlag = true;
        }
    }

    QFuture<Result<OcrResponse>> recognize(OcrRequest, OperationContext context) override {
        m_promise = std::make_unique<QPromise<Result<OcrResponse>>>();
        m_promise->start();
        auto future = m_promise->future();

        m_stopCallback = std::make_unique<std::stop_callback<std::function<void()>>>(context.stopToken, [this]() {
            if (m_completedBeforeDestruction && m_destroyedFlag) {
                *m_completedBeforeDestruction = !(*m_destroyedFlag);
            }
            if (m_promise) {
                m_promise->addResult(std::unexpected(makeApplicationError(
                    ErrorCategory::Cancelled, QStringLiteral("cancelled.shutdown"), QStringLiteral("Shutdown"))));
                m_promise->finish();
            }
        });
        return future;
    }

private:
    bool* m_destroyedFlag{nullptr};
    bool* m_completedBeforeDestruction{nullptr};
    std::unique_ptr<QPromise<Result<OcrResponse>>> m_promise;
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_stopCallback;
};

class StubProjectRepository final : public IProjectRepository {
public:
    QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest, OperationContext) override {
        return makeFailedFuture<ProjectDescriptor>({});
    }
    QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest, OperationContext) override {
        return makeFailedFuture<OpenProjectResult>({});
    }
    QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest, OperationContext) override {
        return makeFailedFuture<OpenProjectResult>({});
    }
    QFuture<Result<SaveProjectResult>> saveProject(SaveProjectRequest request, OperationContext) override {
        if (saveSucceeds) {
            return makeReadyFuture<SaveProjectResult>(SaveProjectResult{request.project.revision()});
        }
        return makeFailedFuture<SaveProjectResult>({});
    }
    QFuture<Result<SaveAsProjectResult>> saveProjectAs(SaveAsProjectRequest, OperationContext) override {
        return makeFailedFuture<SaveAsProjectResult>({});
    }
    QFuture<Result<void>> closeProject(CloseProjectRequest, OperationContext) override {
        return makeReadyFuture<void>({});
    }

    bool saveSucceeds{false};
};

class StubRecentProjectsStore final : public manga::application::IRecentProjectsStore {
public:
    [[nodiscard]] QVector<manga::application::RecentProject> loadRecentProjects() const override {
        return projects;
    }
    void addRecentProject(const manga::application::RecentProject& project) override {
        projects.append(project);
    }
    void removeRecentProject(const QString&) override {}
    void clearRecentProjects() override {}

    QVector<manga::application::RecentProject> projects;
};

class StubAppSettingsStore final : public manga::application::IAppSettingsStore {
public:
    [[nodiscard]] std::optional<QVariant> readSetting(const QString&) const override {
        return std::nullopt;
    }
    void writeSetting(const QString&, const QVariant&) override {}
    void removeSetting(const QString&) override {}
};

class StubThumbnailStore final : public manga::application::IThumbnailStore {
public:
    QFuture<manga::application::Result<QImage>> readThumbnail(manga::application::ReadThumbnailRequest,
                                                              manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<QImage>({});
    }
    QFuture<manga::application::Result<void>> writeThumbnail(manga::application::WriteThumbnailRequest,
                                                             manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<void>({});
    }
};

class StubPageImageLoader final : public manga::application::IPageImageLoader {
public:
    QFuture<manga::application::Result<QImage>> loadPageImage(manga::application::LoadPageImageRequest,
                                                              manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<QImage>({});
    }
};

class StubComicImporter final : public manga::application::IComicImporter {
public:
    QFuture<manga::application::Result<manga::application::ImportImagesResult>>
    importImages(manga::application::ImportImagesRequest, manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<manga::application::ImportImagesResult>({});
    }
    QFuture<manga::application::Result<void>> discardImportedAssets(manga::application::DiscardImportedAssetsRequest,
                                                                    manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<void>({});
    }
    QFuture<manga::application::Result<void>>
    acknowledgeImportedAssets(manga::application::AcknowledgeImportedAssetsRequest,
                              manga::application::OperationContext) override {
        return manga::application::makeFailedFuture<void>({});
    }
};

class StubCredentialStore final : public manga::application::ICredentialStore {
public:
    Result<bool> hasApiKey(const QString&) override {
        return true;
    }
    Result<SecretBytes> readApiKey(const QString&) override {
        return SecretBytes{QByteArrayLiteral("sk-test-key")};
    }
    Result<void> writeApiKey(const QString&, SecretBytes) override {
        return {};
    }
    Result<void> deleteApiKey(const QString&) override {
        return {};
    }
};

class StubOcrModelValidator final : public manga::application::IOcrModelValidator {
public:
    Result<OcrModelStatus> validate(const QString&) override {
        return OcrModelStatus{.isValid = true};
    }
};

} // namespace

class ApplicationContextTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void injectedPortsAreExposed() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto* repositoryPtr = repository.get();
        auto* recentsPtr = recents.get();
        auto* settingsPtr = settings.get();

        manga::app::ApplicationContext context(std::move(repository), std::move(recents), std::move(settings));
        QCOMPARE(context.projectRepository(), repositoryPtr);
        QCOMPARE(context.recentProjectsStore(), recentsPtr);
        QCOMPARE(context.appSettingsStore(), settingsPtr);
        QVERIFY(context.operationRegistry() != nullptr);
    }

    void shutdownIsIdempotent() {
        manga::app::ApplicationContext context(std::make_unique<StubProjectRepository>(),
                                               std::make_unique<StubRecentProjectsStore>(),
                                               std::make_unique<StubAppSettingsStore>());
        context.shutdown();
        context.shutdown(); // The second call must not crash.
        QVERIFY(true);
    }

    void injectedImagePortsAreExposed() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto thumbnails = std::make_unique<StubThumbnailStore>();
        auto images = std::make_unique<StubPageImageLoader>();
        auto importer = std::make_unique<StubComicImporter>();
        auto* thumbnailsPtr = thumbnails.get();
        auto* imagesPtr = images.get();
        auto* importerPtr = importer.get();

        manga::app::ApplicationContext context(std::move(repository),
                                               std::move(recents),
                                               std::move(settings),
                                               std::move(thumbnails),
                                               std::move(images),
                                               std::move(importer),
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr);
        QCOMPARE(context.thumbnailStore(), thumbnailsPtr);
        QCOMPARE(context.pageImageLoader(), imagesPtr);
        QCOMPARE(context.comicImporter(), importerPtr);
    }

    void injectedCredentialAndOcrValidatorPortsAreExposed() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto credentials = std::make_unique<StubCredentialStore>();
        auto validator = std::make_unique<StubOcrModelValidator>();
        auto* credentialsPtr = credentials.get();
        auto* validatorPtr = validator.get();

        manga::app::ApplicationContext context(std::move(repository),
                                               std::move(recents),
                                               std::move(settings),
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               std::move(credentials),
                                               std::move(validator));
        QCOMPARE(context.credentialStore(), credentialsPtr);
        QCOMPARE(context.ocrModelValidator(), validatorPtr);
    }

    void productionContextInstantiatesAdapters() {
        manga::app::ApplicationContext context;
        QVERIFY(context.projectRepository() != nullptr);
        QVERIFY(context.recentProjectsStore() != nullptr);
        QVERIFY(context.appSettingsStore() != nullptr);
        QVERIFY(context.thumbnailStore() != nullptr);
        QVERIFY(context.pageImageLoader() != nullptr);
        QVERIFY(context.comicImporter() != nullptr);
        QVERIFY(context.credentialStore() != nullptr);
        QVERIFY(context.ocrModelValidator() != nullptr);
        QVERIFY(context.operationRegistry() != nullptr);
    }

    void legacyThreeArgInjectionLeavesImagePortsNull() {
        manga::app::ApplicationContext context(std::make_unique<StubProjectRepository>(),
                                               std::make_unique<StubRecentProjectsStore>(),
                                               std::make_unique<StubAppSettingsStore>());
        QVERIFY(context.thumbnailStore() == nullptr); // Editors must tolerate unbound ports in injected test wiring.
        QVERIFY(context.pageImageLoader() == nullptr);
        QVERIFY(context.comicImporter() == nullptr);
        QVERIFY(context.credentialStore() == nullptr);
        QVERIFY(context.ocrModelValidator() == nullptr);
    }

    void productionContextStartsWithoutModelCredentialOrNetwork() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("MangaTranslatorTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ApplicationContextTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
        QSettings settings;
        settings.clear();
        settings.sync();

        manga::app::ApplicationContext context;
        QVERIFY(context.ocrEngine() != nullptr);
        QVERIFY(context.translator() != nullptr);
        QVERIFY(context.comicExporter() != nullptr);

        manga::application::OcrRequest request;
        request.regionId = manga::domain::RegionId::generate();
        QImage testImage(64, 64, QImage::Format_RGB888);
        testImage.fill(Qt::white);
        request.image = testImage;
        request.rect = manga::domain::RectGeometry(0, 0, 64, 64);

        auto future = context.ocrEngine()->recognize(std::move(request), {});
        future.waitForFinished();
        auto result = future.result();
        QVERIFY(!result.has_value());
        QCOMPARE(result.error().category, manga::application::ErrorCategory::Model);
    }

    void manualMainlinePortsAvailableWhenProcessingDegraded() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("MangaTranslatorTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ApplicationContextTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
        QSettings settings;
        settings.clear();
        settings.sync();

        manga::app::ApplicationContext context;
        QVERIFY(context.projectRepository() != nullptr);
        QVERIFY(context.comicImporter() != nullptr);
        QVERIFY(context.pageImageLoader() != nullptr);
        QVERIFY(context.comicExporter() != nullptr);
        QVERIFY(context.thumbnailStore() != nullptr);
        QVERIFY(context.derivedAssetStore() != nullptr);
    }

    void translatorReadsLatestProviderConfigSnapshot() {
        StubCredentialStore store;
        auto transport = std::make_unique<manga::network::test::FakeTransport>();
        manga::network::HttpResponse resp;
        resp.httpStatus = 200;
        resp.body = QByteArrayLiteral(
            "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"items\\\":"
            "[{\\\"regionId\\\":\\\"11111111-1111-4111-8111-111111111111\\\",\\\"sourceText\\\":\\\"こんにちは\\\","
            "\\\"translation\\\":\\\"你好\\\",\\\"candidates\\\":[]}]}\"}}]}");
        transport->responses.push_back(resp);
        transport->responses.push_back(resp);
        auto* rawTransport = transport.get();

        manga::network::ProviderConfig initialConfig;
        initialConfig.providerId = QStringLiteral("provider-1");
        initialConfig.endpoint = QUrl(QStringLiteral("https://api.initial.com/v1/chat/completions"));
        initialConfig.model = QStringLiteral("model-initial");

        auto configSnapshot = std::make_shared<const manga::network::ProviderConfig>(initialConfig);
        auto translator =
            std::make_unique<manga::network::OpenAiCompatibleTranslator>(configSnapshot, &store, std::move(transport));

        manga::application::TranslationRequest req;
        req.sourceLanguage = manga::domain::languages::Japanese;
        req.targetLanguage = manga::domain::languages::SimplifiedChinese;
        manga::application::TranslationRegion r;
        r.regionId = manga::domain::RegionId::parse("11111111-1111-4111-8111-111111111111").value();
        r.sourceText = QStringLiteral("こんにちは");
        req.regions.push_back(r);

        auto future1 = translator->translate(req, {});
        future1.waitForFinished();
        QVERIFY(future1.result().has_value());
        QCOMPARE(rawTransport->requests.size(), 1);
        QCOMPARE(rawTransport->requests.at(0).url(),
                 QUrl(QStringLiteral("https://api.initial.com/v1/chat/completions")));

        // Replace snapshot
        manga::network::ProviderConfig updatedConfig = initialConfig;
        updatedConfig.endpoint = QUrl(QStringLiteral("https://api.updated.com/v1/chat/completions"));
        translator->updateConfig(std::make_shared<const manga::network::ProviderConfig>(updatedConfig));

        auto future2 = translator->translate(req, {});
        future2.waitForFinished();
        QVERIFY(future2.result().has_value());
        QCOMPARE(rawTransport->requests.size(), 2);
        QCOMPARE(rawTransport->requests.at(1).url(),
                 QUrl(QStringLiteral("https://api.updated.com/v1/chat/completions")));
    }

    void shutdownIsIdempotentAndRunsFullTeardown() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto ocr = std::make_unique<manga::application::test::FakeOcrEngine>();
        ocr->deferred = true;

        manga::app::ApplicationContext context(std::move(repository),
                                               std::move(recents),
                                               std::move(settings),
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               std::move(ocr),
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr);

        auto opContext = context.operationRegistry()->createOperation(0, 0);
        manga::application::OcrRequest request;
        request.regionId = manga::domain::RegionId::generate();
        auto future = context.ocrEngine()->recognize(request, opContext);

        QCOMPARE(context.operationRegistry()->inFlightCount(), 1);

        context.shutdown();
        context.shutdown();

        QVERIFY(opContext.stopToken.stop_requested());
    }

    void shutdownCancelsInFlightOperationsBeforeAdapterTeardown() {
        bool adapterDestroyed = false;
        bool completedBeforeDestruction = false;

        auto repository = std::make_unique<StubProjectRepository>();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto trackingOcr =
            std::make_unique<DestructionTrackingOcrEngine>(&adapterDestroyed, &completedBeforeDestruction);

        auto context = std::make_unique<manga::app::ApplicationContext>(std::move(repository),
                                                                        std::move(recents),
                                                                        std::move(settings),
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        std::move(trackingOcr),
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr);

        auto opContext = context->operationRegistry()->createOperation(0, 0);
        manga::application::OcrRequest request;
        request.regionId = manga::domain::RegionId::generate();
        auto future = context->ocrEngine()->recognize(request, opContext);

        QVERIFY(!future.isFinished());

        context.reset();

        QVERIFY(adapterDestroyed);
        QVERIFY(completedBeforeDestruction);
        QVERIFY(future.isFinished());
        QVERIFY(!future.result().has_value());
        QCOMPARE(future.result().error().category, manga::application::ErrorCategory::Cancelled);
    }

    void windowCloseCancelKeepsContextAndSessionAlive() {
        auto repository = std::make_unique<StubProjectRepository>();
        auto* repositoryPtr = repository.get();
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto settings = std::make_unique<StubAppSettingsStore>();
        auto thumbnails = std::make_unique<StubThumbnailStore>();
        auto loader = std::make_unique<StubPageImageLoader>();
        auto importer = std::make_unique<StubComicImporter>();
        auto credentials = std::make_unique<StubCredentialStore>();
        auto validator = std::make_unique<StubOcrModelValidator>();

        auto context = std::make_unique<manga::app::ApplicationContext>(std::move(repository),
                                                                        std::move(recents),
                                                                        std::move(settings),
                                                                        std::move(thumbnails),
                                                                        std::move(loader),
                                                                        std::move(importer),
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        std::move(credentials),
                                                                        std::move(validator));

        int argc = 1;
        char arg0[] = "manga_tests_app";
        char* argv[] = {arg0, nullptr};

        manga::app::Application app(argc, argv, std::move(context));

        bool timerExecuted = false;
        QTimer::singleShot(20, [&]() {
            timerExecuted = true;
            auto* mainWindow = app.mainWindow();
            QVERIFY(mainWindow != nullptr);

            auto* editorPage = mainWindow->findChild<manga::ui::screen::editor::EditorPage*>();
            QVERIFY(editorPage != nullptr);

            auto* editorViewModel = editorPage->viewModelForTest();
            QVERIFY(editorViewModel != nullptr);

            manga::domain::ComicProject project("Test Project");
            auto pageId = manga::domain::PageId::generate();
            project.addPage(manga::domain::ComicPage(pageId, "page_001.png", manga::domain::ImageSize(800, 1200)));
            manga::application::OpenedProjectSession session{
                .project = std::move(project),
                .projectRootPath = QStringLiteral("/fake/path"),
            };
            editorViewModel->openProjectSession(std::move(session));
            QVERIFY(editorViewModel->hasSession());

            editorViewModel->createRegion(manga::domain::RectGeometry(10, 10, 100, 100));
            QVERIFY(editorViewModel->state().saveState.isDirty);

            Q_EMIT mainWindow->closeRequested();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            auto* closeDialog = mainWindow->findChild<fluent::dialogs_flyouts::ContentDialog*>();
            QVERIFY(closeDialog != nullptr);
            auto* message = closeDialog->findChild<fluent::textfields::Label*>(QStringLiteral("projectCloseMessage"));
            QVERIFY(message != nullptr);
            QVERIFY(message->wordWrap());
            QVERIFY(message->text().contains(QStringLiteral("未保存")));
            const auto buttons = closeDialog->findChildren<fluent::basicinput::Button*>();
            QCOMPARE(buttons.size(), 3);
            for (const auto* button : buttons) {
                QCOMPARE(button->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
            }
            QVERIFY(closeDialog->width() >= 480);

            QPointer<fluent::dialogs_flyouts::ContentDialog> dialogGuard(closeDialog);
            closeDialog->setAnimationEnabled(false);
            Q_EMIT closeDialog->closeButtonClicked();
            closeDialog->done(fluent::dialogs_flyouts::ContentDialog::ResultNone);
            QTRY_VERIFY_WITH_TIMEOUT(dialogGuard.isNull(), 2000);

            QVERIFY(editorViewModel->hasSession());
            QVERIFY(editorViewModel->state().saveState.isDirty);
            QVERIFY(mainWindow->isVisible());

            editorViewModel->createRegion(manga::domain::RectGeometry(200, 200, 50, 50));
            QCOMPARE(editorViewModel->state().regions.size(), 2);

            repositoryPtr->saveSucceeds = true;
            Q_EMIT mainWindow->closeRequested();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            auto* saveDialog = mainWindow->findChild<fluent::dialogs_flyouts::ContentDialog*>();
            QVERIFY(saveDialog != nullptr);
            saveDialog->setAnimationEnabled(false);
            Q_EMIT saveDialog->primaryButtonClicked();
            saveDialog->done(fluent::dialogs_flyouts::ContentDialog::ResultPrimary);
            QTRY_VERIFY_WITH_TIMEOUT(!editorViewModel->hasSession(), 2000);
        });

        const int exitCode = app.run();
        QVERIFY(timerExecuted);
        QCOMPARE(exitCode, 0);
    }

    void importedProjectIsAddedToRecentProjects() {
        auto recents = std::make_unique<StubRecentProjectsStore>();
        auto* recentsPtr = recents.get();
        auto context = std::make_unique<manga::app::ApplicationContext>(std::make_unique<StubProjectRepository>(),
                                                                        std::move(recents),
                                                                        std::make_unique<StubAppSettingsStore>(),
                                                                        std::make_unique<StubThumbnailStore>(),
                                                                        std::make_unique<StubPageImageLoader>(),
                                                                        std::make_unique<StubComicImporter>(),
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        nullptr,
                                                                        std::make_unique<StubCredentialStore>(),
                                                                        std::make_unique<StubOcrModelValidator>());

        int argc = 1;
        char arg0[] = "manga_tests_app";
        char* argv[] = {arg0, nullptr};
        manga::app::Application app(argc, argv, std::move(context));

        bool timerExecuted = false;
        QTimer::singleShot(20, [&]() {
            timerExecuted = true;
            auto* importViewModel = app.mainWindow()->findChild<manga::ui::screen::import_::ImportViewModel*>();
            QVERIFY(importViewModel != nullptr);

            manga::application::OpenedProjectSession session;
            session.project = manga::domain::ComicProject("Imported Project");
            session.projectRootPath = QStringLiteral("D:/projects/imported");
            Q_EMIT importViewModel->projectImported(session);

            QCOMPARE(recentsPtr->projects.size(), 1);
            QCOMPARE(recentsPtr->projects.first().name, QStringLiteral("Imported Project"));
            QCOMPARE(recentsPtr->projects.first().path, QStringLiteral("D:/projects/imported"));
            QVERIFY(recentsPtr->projects.first().lastOpenedTimeMs > 0);
            QCoreApplication::quit();
        });

        QCOMPARE(app.run(), 0);
        QVERIFY(timerExecuted);
    }
};

QTEST_MAIN(ApplicationContextTest)
#include "application_context_test.moc"
