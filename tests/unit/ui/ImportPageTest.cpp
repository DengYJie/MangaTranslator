#include "ui/screen/import/ImportPage.h"

// TODO(Defect 2): FakeComicImporter.h is located in tests/unit/application/, ui target missing include path.
#include "FakeComicImporter.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/import/FileDropZone.h"
#include "ui/screen/import/ImportViewModel.h"

#include <FluentQt/FluentQt.h>
#include <QScrollArea>
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

class ImportPageTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ViewModel tests

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
        const auto session = importedSpy.first().first().value<application::OpenedProjectSession>();
        QCOMPARE(QString::fromStdString(session.project.name()), QStringLiteral("我的漫画"));
        QCOMPARE(session.project.pages().size(), 3);
        QCOMPARE(QString::fromStdString(session.project.pages().front().originalAssetPath()),
                 QStringLiteral("assets/original/page_1000.png"));
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
        const auto session = importedSpy.first().first().value<application::OpenedProjectSession>();
        QCOMPARE(session.project.pages().size(), 2);
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

    // Page and widget tests

    void contentUsesFluentCardsOnTransparentScrollSurface() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);

        auto* sourceCard = page.findChild<fluent::layout::Card*>(QStringLiteral("importSourceCard"));
        auto* settingsCard = page.findChild<fluent::layout::Card*>(QStringLiteral("importSettingsCard"));
        auto* scrollArea = page.findChild<QScrollArea*>(QStringLiteral("importScrollArea"));

        QVERIFY(sourceCard != nullptr);
        QVERIFY(settingsCard != nullptr);
        QVERIFY(scrollArea != nullptr);
        QCOMPARE(sourceCard->appearance(), fluent::layout::Card::Layer);
        QCOMPARE(settingsCard->appearance(), fluent::layout::Card::Layer);
        QCOMPARE(scrollArea->viewport()->palette().color(QPalette::Base), QColor(Qt::transparent));
    }

    void submitButtonTracksFormValidation() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();

        // Stage 1: no sources, no save location, no name.
        QVERIFY(!page.submitButtonForTest()->isEnabled());

        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit(); // Validate form and set errors

        // Stage 2: sources and location are set while the name is still empty.
        QVERIFY(!page.submitButtonForTest()->isEnabled());

        viewModel.setProjectName(QStringLiteral("项目"));
        // Stage 3: every required field is valid, so no validation error remains.
        QVERIFY(page.submitButtonForTest()->isEnabled());
        QCOMPARE(page.submitButtonForTest()->text(), QStringLiteral("创建并导入"));
    }

    void dropZoneEmitsDroppedPaths() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();
        QSignalSpy spy(page.dropZoneForTest(), &ui::screen::import_::FileDropZone::pathsDropped);
        auto* mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(QStringLiteral("D:/src/a.jpg")),
                       QUrl::fromLocalFile(QStringLiteral("D:/src/folder"))});

        QDragEnterEvent dragEnter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(page.dropZoneForTest(), &dragEnter);

        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(page.dropZoneForTest(), &drop);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().value<QVector<QString>>().size(), 2);
    }

    void importingStateShowsProgressAndCancelButton() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        importer.deferred = true;
        application::OperationRegistry registry;
        ui::screen::import_::ImportViewModel viewModel(&repository, &importer, &registry);
        ui::screen::import_::ImportPage page(&viewModel);
        page.show();
        viewModel.addSourcePaths({QStringLiteral("D:/src/a.jpg")});
        viewModel.setProjectName(QStringLiteral("进行中"));
        viewModel.setSaveLocation(QStringLiteral("D:/projects"));
        viewModel.submit();
        QTRY_VERIFY_WITH_TIMEOUT(importer.callCount == 1, 4000);
        QVERIFY(page.cancelImportButtonForTest()->isVisible());
        QVERIFY(page.progressBarForTest()->isVisible());
        QVERIFY(!page.submitButtonForTest()->isEnabled());
        viewModel.cancelImport();
        importer.complete();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().importInProgress, 4000);
    }
};

QTEST_MAIN(ImportPageTest)
#include "ImportPageTest.moc"
