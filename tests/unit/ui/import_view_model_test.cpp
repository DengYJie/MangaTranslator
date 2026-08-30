#include "FakeComicImporter.h" // tests/unit/application, via include directory
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/import/ImportViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::screen::import_::ImportViewModel;

namespace {

application::ImportImagesResult makeImportResult(int pageCount, int failureCount = 0) {
    application::ImportImagesResult result;
    result.batchId = application::OperationId::generate();
    for (int i = 0; i < pageCount; ++i) {
        application::ImportedPage page;
        page.page = domain::ComicPage(domain::PageId::generate(),
                                      "assets/original/page_" + std::to_string(1000 + i) + ".png",
                                      domain::ImageSize{1200, 1800},
                                      i);
        page.sourcePath = QStringLiteral("D:/src/page_%1.png").arg(i);
        result.importedPages.append(page);
    }
    for (int i = 0; i < failureCount; ++i) {
        application::ImportFailure failure;
        failure.path = QStringLiteral("D:/src/broken_%1.png").arg(i);
        failure.error = application::makeApplicationError(
            application::ErrorCategory::Input, QStringLiteral("input.decode_failed"), QStringLiteral("无法解码图像"));
        result.failures.append(failure);
    }
    return result;
}

struct Fixture {
    test::FakeRecentProjectsStore recents;
    test::FakeProjectRepository repository;
    application::test::FakeComicImporter importer;
    application::OperationRegistry registry;
};

} // namespace

class ImportViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void addSourcesValidatesFormatsAndDeduplicates() {
        Fixture f;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg"),
                                  QStringLiteral("D:/src/b.PNG"),
                                  QStringLiteral("D:/src/c.txt"),
                                  QStringLiteral("D:/src/folder")});

        const auto& sources = viewModel.state().sources;
        QCOMPARE(sources.size(), 4);
        QVERIFY(sources.at(0).supported);
        QVERIFY(sources.at(1).supported);
        QVERIFY(!sources.at(2).supported);
        QVERIFY(sources.at(3).isFolder);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        QCOMPARE(viewModel.state().sources.size(), 4);
    }

    void submitWithoutNameFailsValidation() {
        Fixture f;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QVERIFY(!viewModel.state().nameError.isEmpty());
        QVERIFY(viewModel.state().formErrorCount > 0);
        QVERIFY(f.repository.lastCreatedPath.isEmpty());
    }

    void happyPathCreatesImportsSavesAndEmitsSession() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(3);
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("我的漫画"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importedSpy.size() == 1, 4000);
        const auto session = importedSpy.first().first().value<application::ProjectSessionDescriptor>();
        QCOMPARE(session.projectName, QStringLiteral("我的漫画"));
        QCOMPARE(session.pages.size(), 3);
        QCOMPARE(session.pages.first().originalAssetPath, QStringLiteral("assets/original/page_1000.png"));
        QCOMPARE(f.importer.callCount, 1);
        QCOMPARE(f.importer.acknowledgeCallCount, 1);
        QCOMPARE(f.repository.saveCallCount, 1);
        QCOMPARE(f.repository.closeCallCount, 0);
        QVERIFY(!viewModel.state().importInProgress);
        QCOMPARE(f.repository.lastCreatedPath, QStringLiteral("D:/projects/我的漫画"));
    }

    void zeroImportedPagesFailsWithoutSaving() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(0, 2);
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("空项目"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 4000);
        QCOMPARE(importedSpy.size(), 0);
        QCOMPARE(f.repository.saveCallCount, 0);
        QCOMPARE(f.importer.acknowledgeCallCount, 0);
        QTRY_VERIFY_WITH_TIMEOUT(f.repository.closeCallCount == 1, 2000);
    }

    void cancelDuringImportRollsBackAndKeepsForm() {
        Fixture f;
        f.importer.deferred = true;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("可取消"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(f.importer.callCount == 1, 4000);
        viewModel.cancelImport();
        f.importer.complete();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
        QCOMPARE(importedSpy.size(), 0);
        QCOMPARE(viewModel.state().projectName, QStringLiteral("可取消"));
        QVERIFY(!viewModel.state().notice.has_value());
        QTRY_VERIFY_WITH_TIMEOUT(f.repository.closeCallCount == 1, 2000);
    }

    void partialFailuresAreCarriedAsWarnings() {
        Fixture f;
        f.importer.nextResponse = makeImportResult(2, 1);
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        QSignalSpy importedSpy(&viewModel, &ImportViewModel::projectImported);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("部分成功"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importedSpy.size() == 1, 4000);
        const auto session = importedSpy.first().first().value<application::ProjectSessionDescriptor>();
        QCOMPARE(session.pages.size(), 2);
        QCOMPARE(session.warnings.size(), 1);
        QCOMPARE(session.warnings.first().code, QStringLiteral("input.decode_failed"));
    }

    void createFailureMapsToLocationError() {
        Fixture f;
        f.repository.failCreate = true;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("已存在"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().locationError.isEmpty() || viewModel.state().notice.has_value(),
                                 4000);
        QCOMPARE(f.importer.callCount, 0);
    }

    void concurrentSubmitIsIgnored() {
        Fixture f;
        f.importer.deferred = true;
        ImportViewModel viewModel(&f.repository, &f.importer, &f.registry);
        viewModel.addSourcePaths({QStringLiteral("D:/src")});
        viewModel.setProjectName(QStringLiteral("防抖"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(f.importer.callCount == 1, 4000);
        QCOMPARE(f.importer.callCount, 1);
        viewModel.cancelImport();
        f.importer.complete();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
    }
};

QTEST_GUILESS_MAIN(ImportViewModelTest)
#include "import_view_model_test.moc"
