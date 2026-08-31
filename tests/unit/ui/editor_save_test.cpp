#include "application/ports/IProjectRepository.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/command/RegionCommands.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QPromise>
#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using namespace manga::ui::screen::editor;

class DelayProjectRepository : public application::IProjectRepository {
public:
    std::shared_ptr<QPromise<application::Result<application::SaveProjectResult>>> savePromise;

    QFuture<application::Result<application::ProjectDescriptor>> createProject(application::CreateProjectRequest,
                                                                               application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::OpenProjectResult>> openProject(application::OpenProjectRequest,
                                                                             application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::OpenProjectResult>>
    recoverProject(application::OpenProjectRequest, application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::SaveAsProjectResult>>
    saveProjectAs(application::SaveAsProjectRequest, application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<void>> closeProject(application::CloseProjectRequest,
                                                    application::OperationContext) override {
        return {};
    }

    QFuture<application::Result<application::SaveProjectResult>> saveProject(application::SaveProjectRequest request,
                                                                             application::OperationContext) override {
        savePromise = std::make_shared<QPromise<application::Result<application::SaveProjectResult>>>();
        return savePromise->future();
    }
};

class EditorSaveTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ProjectCloseSummary>();
        qRegisterMetaType<application::ApplicationError>();
    }

    void saveCompletionDoesNotCleanNewerRevision() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);

        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        domain::ComicPage page(domain::PageId::generate(), "page1.png", {1000, 1500}, 0);
        domain::TextRegion region(domain::RegionId::generate(), {10, 10, 100, 100}, 0);
        page.addRegion(region);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        // Make it dirty
        viewModel.selectRegion(region.id(), RegionSelectionMode::Replace);
        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::FontFamily, QStringLiteral("Arial"));

        QVERIFY(viewModel.state().saveState.isDirty);

        // Trigger save
        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);

        const quint64 capturedRevision = viewModel.currentProjectRevision();

        // Continue editing during save
        viewModel.commitInspectorFieldEdit(page.id(), {region.id()}, InspectorField::FontSizePt, 20.0);

        // Complete save with older revision
        repository.savePromise->addResult(application::SaveProjectResult{capturedRevision});
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(viewModel.state().saveState.isDirty);
        QVERIFY(!viewModel.undoStackIsCleanForTest());
    }

    void cleanSave() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        domain::ComicPage page(domain::PageId::generate(), "page1.png", {1000, 1500}, 0);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        // Dirty it
        viewModel.createRegion(domain::RectGeometry(10, 10, 100, 100));
        QVERIFY(viewModel.state().saveState.isDirty);

        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);
        const quint64 rev = viewModel.currentProjectRevision();
        repository.savePromise->addResult(application::SaveProjectResult{rev});
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(!viewModel.state().saveState.isDirty);
        QVERIFY(viewModel.undoStackIsCleanForTest());
    }

    void saveFailureKeepsDirty() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        domain::ComicPage page(domain::PageId::generate(), "page1.png", {1000, 1500}, 0);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        viewModel.createRegion(domain::RectGeometry(10, 10, 100, 100));
        QVERIFY(viewModel.state().saveState.isDirty);

        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);

        repository.savePromise->addResult(std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Persistence, "test", "fail")));
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(viewModel.state().saveState.isDirty);
    }

    void saveSealsTextEdit() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        // Force the clock to not advance, so normally it would merge
        viewModel.setTextEditClockForTest([]() { return 1000; });
        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        domain::ComicPage page(domain::PageId::generate(), "page1.png", {1000, 1500}, 0);
        domain::TextRegion region(domain::RegionId::generate(), {10, 10, 100, 100}, 0);
        page.addRegion(region);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());
        viewModel.selectRegion(region.id(), RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::ManualTranslation, QStringLiteral("A"));
        QCOMPARE(viewModel.undoStackCountForTest(), 1);

        viewModel.saveProject();

        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::ManualTranslation, QStringLiteral("B"));
        QCOMPARE(viewModel.undoStackCountForTest(), 2); // Did not merge because save sealed it
    }
};

QTEST_MAIN(EditorSaveTest)
#include "editor_save_test.moc"
