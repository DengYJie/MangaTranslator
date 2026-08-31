#include "app/ApplicationContext.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/ICredentialStore.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IInpainter.h"
#include "application/ports/IOcrEngine.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/ports/ITextLayouter.h"
#include "application/ports/IThumbnailStore.h"
#include "application/ports/ITranslator.h"

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

} // namespace

class ApplicationContextTest : public QObject {
    Q_OBJECT
private slots:
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
                                               nullptr);
        QCOMPARE(context.thumbnailStore(), thumbnailsPtr);
        QCOMPARE(context.pageImageLoader(), imagesPtr);
        QCOMPARE(context.comicImporter(), importerPtr);
    }

    void legacyThreeArgInjectionLeavesImagePortsNull() {
        manga::app::ApplicationContext context(std::make_unique<StubProjectRepository>(),
                                               std::make_unique<StubRecentProjectsStore>(),
                                               std::make_unique<StubAppSettingsStore>());
        QVERIFY(context.thumbnailStore() == nullptr); // 编辑器必须容忍空端口（测试注入场景）
        QVERIFY(context.pageImageLoader() == nullptr);
        QVERIFY(context.comicImporter() == nullptr);
    }
};

QTEST_GUILESS_MAIN(ApplicationContextTest)
#include "application_context_test.moc"
