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
    }
};

QTEST_MAIN(EditorSaveTest)
#include "editor_save_test.moc"
