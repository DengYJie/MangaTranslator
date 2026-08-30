#include "app/ApplicationContext.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"

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
};

QTEST_GUILESS_MAIN(ApplicationContextTest)
#include "application_context_test.moc"
