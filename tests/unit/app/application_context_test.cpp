#include "FakeTransport.h"
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
#include "domain/model/LanguageCode.h"
#include "domain/model/TextRegion.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

using namespace manga::application;

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
    QFuture<Result<SaveProjectResult>> saveProject(SaveProjectRequest, OperationContext) override {
        return makeFailedFuture<SaveProjectResult>({});
    }
    QFuture<Result<SaveAsProjectResult>> saveProjectAs(SaveAsProjectRequest, OperationContext) override {
        return makeFailedFuture<SaveAsProjectResult>({});
    }
    QFuture<Result<void>> closeProject(CloseProjectRequest, OperationContext) override {
        return makeReadyFuture<void>({});
    }
};

class StubRecentProjectsStore final : public manga::application::IRecentProjectsStore {
public:
    [[nodiscard]] QVector<manga::application::RecentProject> loadRecentProjects() const override {
        return {};
    }
    void addRecentProject(const manga::application::RecentProject&) override {}
    void removeRecentProject(const QString&) override {}
    void clearRecentProjects() override {}
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
};

QTEST_GUILESS_MAIN(ApplicationContextTest)
#include "application_context_test.moc"
