#include "FakeComicExporter.h"
#include "FakeDerivedAssetStore.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::application::ExportScope;
using manga::ui::screen::editor::EditorViewModel;

namespace {

/// Creates a session with `pageCount` pages; page i carries asset path page_10xx.png
/// and order index i, so the UseCase-derived output stems are deterministic.
application::OpenedProjectSession makeSession(int pageCount) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Export Demo");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    for (int i = 0; i < pageCount; ++i) {
        auto pageId = domain::PageId::generate();
        domain::ComicPage page(pageId,
                               QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                               domain::ImageSize{1200, 1800},
                               i);
        session.project.addPage(std::move(page));
    }
    return session;
}

domain::PageId pageAt(const application::OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

struct Fixture {
    manga::test::FakeThumbnailStore thumbnailStore;
    manga::test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
    application::test::FakeDerivedAssetStore derivedStore;
    application::test::FakeComicExporter exporter;

    Fixture() {
        imageLoader.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        imageLoader.nextImage.fill(Qt::gray);
    }

    void attach(EditorViewModel& viewModel) {
        viewModel.setExportDependencies(&exporter, &derivedStore);
    }
};

} // namespace

class EditorExportTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<application::ApplicationError>();
        qRegisterMetaType<application::ExportedPage>();
        qRegisterMetaType<application::ExportFailure>();
        qRegisterMetaType<application::ExportResponse>();
    }

    void exportWithoutProjectDoesNothing() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));

        QCOMPARE(completedSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.callCount, 0);
    }

    void exportWithoutPagesDoesNothing() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        viewModel.openProjectSession(makeSession(0));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));
        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out"));
        viewModel.exportPages(ExportScope::SelectedPages, QStringLiteral("D:/out"));

        QCOMPARE(completedSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.callCount, 0);
    }

    void exportWithoutDependenciesEmitsFailed() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportCurrentPageBuildsSinglePageRequest() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.selectPage(pageAt(session, 1));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out/current"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(f.exporter.callCount, 1);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.lastRequest.outputDir, QStringLiteral("D:/out/current"));
        QCOMPARE(f.exporter.lastRequest.pages.size(), 1);
        const auto& page = f.exporter.lastRequest.pages.first();
        QCOMPARE(page.pageId, pageAt(session, 1));
        QCOMPARE(page.outputStem, QStringLiteral("page_0002_translated"));
        QVERIFY(!page.sourceImage.isNull());
        QCOMPARE(page.sourceImage.size(), QSize(64, 96));
    }

    void exportSelectedPagesBuildsRequestInProjectOrder() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.setSelectedPages({pageAt(session, 2), pageAt(session, 0)});
        QCOMPARE(viewModel.state().selectedPageIds.size(), 2);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::SelectedPages, QStringLiteral("D:/out/selection"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 2);
        // Explicit selection order {page3, page1} resolves to project page order.
        QCOMPARE(f.exporter.lastRequest.pages[0].pageId, pageAt(session, 0));
        QCOMPARE(f.exporter.lastRequest.pages[1].pageId, pageAt(session, 2));
        QCOMPARE(f.exporter.lastRequest.pages[0].outputStem, QStringLiteral("page_0001_translated"));
        QCOMPARE(f.exporter.lastRequest.pages[1].outputStem, QStringLiteral("page_0003_translated"));
    }

    void exportAllPagesBuildsFullRequest() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/all"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 3);
        QCOMPARE(f.exporter.lastRequest.pages[0].pageId, pageAt(session, 0));
        QCOMPARE(f.exporter.lastRequest.pages[1].pageId, pageAt(session, 1));
        QCOMPARE(f.exporter.lastRequest.pages[2].pageId, pageAt(session, 2));
    }

    void exportIsNotReentrantWhileRunning() {
        Fixture f;
        f.exporter.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/reentry"));
        QVERIFY(viewModel.state().isExporting);
        // The deferred exporter owns the in-flight future once the snapshot stage lands.
        QTRY_COMPARE_WITH_TIMEOUT(f.exporter.callCount, 1, 5000);

        // The second intent must be ignored while the first export is in flight.
        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/reentry"));
        QCOMPARE(f.exporter.callCount, 1);
        QVERIFY(viewModel.state().isExporting);

        f.exporter.complete();
        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportCurrentPageCapturesPageAtIntentTime() {
        Fixture f;
        // Stall the snapshot stage so the page switch happens while the export runs.
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.selectPage(pageAt(session, 1));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out/race"));
        QVERIFY(viewModel.state().isExporting);

        // Page navigation stays enabled during export; the running export must keep
        // exporting the page selected at intent time.
        viewModel.selectPage(pageAt(session, 2));
        QCOMPARE(viewModel.state().currentPageId, pageAt(session, 2));
        QVERIFY(viewModel.state().isExporting);

        f.imageLoader.completeAll();
        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 1);
        QCOMPARE(f.exporter.lastRequest.pages.first().pageId, pageAt(session, 1));
        QCOMPARE(f.exporter.lastRequest.pages.first().outputStem, QStringLiteral("page_0002_translated"));
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportFailureEmitsFailedAndKeepsProject() {
        Fixture f;
        f.exporter.shouldFail = true;
        f.exporter.nextError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                 QStringLiteral("export.write_failed"),
                                                                 QStringLiteral("The export target is not writable"),
                                                                 application::RecoveryAction::ChoosePath);
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        const quint64 revisionBefore = viewModel.currentProjectRevision();
        const bool dirtyBefore = viewModel.state().saveState.isDirty;
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/failure"));

        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
        QCOMPARE(completedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        // A failed export presents a recovery notice but never touches project content.
        QVERIFY(viewModel.state().notice.has_value());
        QVERIFY(!viewModel.state().notice->retryable);
        QCOMPARE(viewModel.currentProjectRevision(), revisionBefore);
        QCOMPARE(viewModel.state().saveState.isDirty, dirtyBefore);
    }
};

QTEST_MAIN(EditorExportTest)
#include "editor_export_test.moc"
