#include "ProjectUseCasesTest.h"

#include "FakeProjectRepository.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

void ProjectUseCasesTest::testSuccessfulCreate() {
    FakeProjectRepository repo;
    CreateProjectUseCase useCase(&repo);

    CreateProjectRequest req;
    req.name = "Test";
    req.path = "/test";
    req.sourceLanguage = languages::Japanese;
    req.targetLanguage = languages::English;

    repo.nextDescriptor.id = ProjectId::generate();

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(repo.callCount, 1);
}

void ProjectUseCasesTest::testCreateFailure() {
    FakeProjectRepository repo;
    CreateProjectUseCase useCase(&repo);

    CreateProjectRequest req;
    // Empty name should fail

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Input);
    QCOMPARE(repo.callCount, 0);
}

void ProjectUseCasesTest::testRemainingProjectOperationsDispatch() {
    FakeProjectRepository repository;
    const ComicProject project("Test", ProjectId::generate());
    const OperationContext context;

    OpenProjectUseCase open(&repository);
    SaveProjectUseCase save(&repository);
    SaveProjectAsUseCase saveAs(&repository);
    CloseProjectUseCase close(&repository);

    QVERIFY(open.execute({"project.json"}, context).result().has_value());
    QVERIFY(save.execute({project}, context).result().has_value());
    QVERIFY(saveAs.execute({project, "copy/project.json"}, context).result().has_value());
    QVERIFY(close.execute({project.id()}, context).result().has_value());
    QCOMPARE(repository.callCount, 4);
    QCOMPARE(repository.openCallCount, 1);
}

void ProjectUseCasesTest::testInvalidProjectOperationsDoNotReachRepository() {
    FakeProjectRepository repository;
    OpenProjectUseCase open(&repository);
    SaveProjectUseCase save(&repository);
    SaveProjectAsUseCase saveAs(&repository);
    CloseProjectUseCase close(&repository);
    RecoverProjectUseCase recover(&repository);
    const ComicProject validProject("Test", ProjectId::generate());
    ComicProject invalidProject;
    invalidProject.setName({});

    QVERIFY(!open.execute({}, {}).result().has_value());
    QVERIFY(!save.execute({invalidProject}, {}).result().has_value());
    QVERIFY(!saveAs.execute({validProject, {}}, {}).result().has_value());
    QVERIFY(!close.execute({}, {}).result().has_value());
    QVERIFY(!recover.execute({}, {}).result().has_value());
    QCOMPARE(repository.callCount, 0);
}

void ProjectUseCasesTest::testRepositoryFailurePropagates() {
    FakeProjectRepository repository;
    repository.shouldFail = true;
    repository.nextError =
        makeApplicationError(ErrorCategory::Persistence, "persistence.failed", "Project storage failed");
    OpenProjectUseCase useCase(&repository);

    const auto result = useCase.execute({"project.json"}, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Persistence);
    QCOMPARE(repository.callCount, 1);
}

void ProjectUseCasesTest::testPreCancelledOperationRejected() {
    FakeProjectRepository repository;
    SaveProjectUseCase useCase(&repository);
    std::stop_source stopSource;
    stopSource.request_stop();
    OperationContext context;
    context.stopToken = stopSource.get_token();

    const auto result = useCase.execute({ComicProject("Test", ProjectId::generate())}, context).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Cancelled);
    QCOMPARE(repository.callCount, 0);
}

void ProjectUseCasesTest::testRecoverUsesRecoveryPort() {
    FakeProjectRepository repository;
    RecoverProjectUseCase useCase(&repository);
    OpenProjectRequest request;
    request.path = "project.json";

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(repository.openCallCount, 0);
    QCOMPARE(repository.recoverCallCount, 1);
}
