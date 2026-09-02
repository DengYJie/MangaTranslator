#include "IntegrationHarness.h"
#include "application/dto/ExportDto.h"
#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

/// Real repository, loader, derived store and exporter with a three-page project
/// imported from the registered fixtures. Only the output directory is injected.
struct ExportFixture {
    RealDataStack stack;
    QString projectRoot;
    std::unique_ptr<EditorViewModel> viewModel;
    QVector<domain::PageId> pageIds;

    [[nodiscard]] bool init(const QString& scenarioName, int pageCount) {
        projectRoot = stack.projectRoot(scenarioName);

        CreateProjectRequest createReq{
            .name = scenarioName,
            .path = projectRoot,
            .sourceLanguage = domain::languages::Japanese,
            .targetLanguage = domain::languages::English,
            .readingDirection = domain::ReadingDirection::RightToLeft,
        };
        auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
        if (!awaitFuture(createFuture) || !createFuture.result().has_value()) {
            return false;
        }

        OpenProjectRequest openReq{.path = projectRoot};
        auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
        if (!awaitFuture(openFuture) || !openFuture.result().has_value()) {
            return false;
        }
        auto project = openFuture.result().value().project;

        QStringList paths;
        for (int i = 1; i <= pageCount; ++i) {
            const QString pagePath =
                QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_00%1.png").arg(i));
            if (!QFileInfo::exists(pagePath)) {
                return false;
            }
            paths.append(pagePath);
        }

        ImportImagesRequest importReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .filePaths = paths,
        };
        auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
        if (!awaitFuture(importFuture) || !importFuture.result().has_value()) {
            return false;
        }

        ApplyImportResultRequest applyReq{
            .projectId = project.id(),
            .result = importFuture.result().value(),
            .sessionGeneration = 0,
            .currentSessionGeneration = 0,
        };
        auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
        if (!applyRes.has_value() || applyRes.value().size() != pageCount) {
            return false;
        }

        AcknowledgeImportedAssetsRequest ackReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .batchId = importFuture.result().value().batchId,
        };
        auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
        if (!awaitFuture(ackFuture) || !ackFuture.result().has_value()) {
            return false;
        }

        SaveProjectRequest saveReq{.project = project};
        auto saveFuture = SaveProjectUseCase(stack.repository.get()).execute(saveReq, OperationContext{});
        if (!awaitFuture(saveFuture) || !saveFuture.result().has_value()) {
            return false;
        }
        if (!project.markCleanIfRevision(saveFuture.result().value().savedRevision)) {
            return false;
        }

        for (const auto& page : project.pages()) {
            pageIds.append(page.id());
        }
        if (pageIds.size() != pageCount) {
            return false;
        }

        viewModel = std::make_unique<EditorViewModel>(
            stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
        viewModel->setExportDependencies(stack.exporter.get(), stack.derivedAssetStore.get());

        OpenedProjectSession session{
            .project = project,
            .projectRootPath = projectRoot,
            .recoverySource = RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(session);
        return viewModel->hasSession() && viewModel->state().pages.size() == pageCount;
    }

    /// Runs one export through the ViewModel and returns true when it completes.
    [[nodiscard]] bool runExportToCompletion(ExportScope scope, const QString& outputDir, ExportResponse& response) {
        QSignalSpy completedSpy(viewModel.get(), &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(viewModel.get(), &EditorViewModel::exportFailed);
        viewModel->exportPages(scope, outputDir);
        if (!completedSpy.wait(kAwaitTimeoutMs)) {
            return false;
        }
        if (failedSpy.count() != 0) {
            return false;
        }
        response = completedSpy.first().first().value<ExportResponse>();
        return true;
    }

    /// Lists PNG file names directly inside outputDir.
    [[nodiscard]] QStringList pngFileNames(const QString& outputDir) const {
        return QDir(outputDir).entryList(QStringList{QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    }
};

} // namespace

class PageExportScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<application::ApplicationError>();
        qRegisterMetaType<application::ExportedPage>();
        qRegisterMetaType<application::ExportFailure>();
        qRegisterMetaType<application::ExportResponse>();
    }

    void exportCurrentPageOnly();
    void exportSelectedPagesPreservesProjectOrder();
    void exportAllPagesWritesEveryPage();
    void conflictFileNamesAppendSuffixWithoutOverwrite();
    void singlePageFailureKeepsOtherOutputsAndProject();
};

void PageExportScenarioTest::exportCurrentPageOnly() {
    ExportFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e05_current_page"), 3));

    const auto currentPageId = fixture.pageIds.at(1);
    fixture.viewModel->selectPage(currentPageId);
    QCOMPARE(fixture.viewModel->state().currentPageId, currentPageId);

    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    ExportResponse response;
    QVERIFY(fixture.runExportToCompletion(ExportScope::CurrentPage, outputDir.path(), response));

    QCOMPARE(response.failures.size(), 0);
    QCOMPARE(response.exported.size(), 1);
    QCOMPARE(response.exported.first().pageId, currentPageId);
    QCOMPARE(QFileInfo(response.exported.first().path).fileName(), QStringLiteral("page_0002_translated.png"));
    QVERIFY(QFileInfo::exists(response.exported.first().path));
    QCOMPARE(fixture.pngFileNames(outputDir.path()).size(), 1);
}

void PageExportScenarioTest::exportSelectedPagesPreservesProjectOrder() {
    ExportFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e05_selected_pages"), 3));

    // Explicit selection {page3, page1}: the output order must follow project page order.
    fixture.viewModel->setSelectedPages({fixture.pageIds.at(2), fixture.pageIds.at(0)});

    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    ExportResponse response;
    QVERIFY(fixture.runExportToCompletion(ExportScope::SelectedPages, outputDir.path(), response));

    QCOMPARE(response.failures.size(), 0);
    QCOMPARE(response.exported.size(), 2);
    QCOMPARE(response.exported[0].pageId, fixture.pageIds.at(0));
    QCOMPARE(response.exported[1].pageId, fixture.pageIds.at(2));
    const QStringList names = fixture.pngFileNames(outputDir.path());
    QCOMPARE(names.size(), 2);
    QCOMPARE(names[0], QStringLiteral("page_0001_translated.png"));
    QCOMPARE(names[1], QStringLiteral("page_0003_translated.png"));
}

void PageExportScenarioTest::exportAllPagesWritesEveryPage() {
    ExportFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e05_all_pages"), 3));

    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    ExportResponse response;
    QVERIFY(fixture.runExportToCompletion(ExportScope::AllPages, outputDir.path(), response));

    QCOMPARE(response.failures.size(), 0);
    QCOMPARE(response.exported.size(), 3);
    QCOMPARE(response.exported[0].pageId, fixture.pageIds.at(0));
    QCOMPARE(response.exported[1].pageId, fixture.pageIds.at(1));
    QCOMPARE(response.exported[2].pageId, fixture.pageIds.at(2));
    const QStringList names = fixture.pngFileNames(outputDir.path());
    QCOMPARE(names.size(), 3);
    QCOMPARE(names[0], QStringLiteral("page_0001_translated.png"));
    QCOMPARE(names[1], QStringLiteral("page_0002_translated.png"));
    QCOMPARE(names[2], QStringLiteral("page_0003_translated.png"));
    for (const auto& exported : response.exported) {
        QVERIFY(QFileInfo(exported.path).size() > 0);
    }
}

void PageExportScenarioTest::conflictFileNamesAppendSuffixWithoutOverwrite() {
    ExportFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e05_conflict"), 3));

    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    const QString preexistingPath = QDir(outputDir.path()).filePath(QStringLiteral("page_0001_translated.png"));
    {
        QFile preexisting(preexistingPath);
        QVERIFY(preexisting.open(QIODevice::WriteOnly));
        const QByteArray marker = QByteArrayLiteral("preexisting bytes");
        QCOMPARE(preexisting.write(marker), marker.size());
    }
    QFile beforeFile(preexistingPath);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray preexistingDigest =
        QCryptographicHash::hash(beforeFile.readAll(), QCryptographicHash::Sha256).toHex();

    ExportResponse response;
    QVERIFY(fixture.runExportToCompletion(ExportScope::AllPages, outputDir.path(), response));

    QCOMPARE(response.failures.size(), 0);
    QCOMPARE(response.exported.size(), 3);
    // The conflicting page got a suffix; the old file kept its exact bytes.
    QCOMPARE(QFileInfo(response.exported[0].path).fileName(), QStringLiteral("page_0001_translated_001.png"));
    QFile afterFile(preexistingPath);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(afterFile.readAll(), QCryptographicHash::Sha256).toHex(), preexistingDigest);
    QCOMPARE(QFileInfo(response.exported[1].path).fileName(), QStringLiteral("page_0002_translated.png"));
    QCOMPARE(fixture.pngFileNames(outputDir.path()).size(), 4);
}

void PageExportScenarioTest::singlePageFailureKeepsOtherOutputsAndProject() {
    ExportFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e05_page_failure"), 3));

    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);
    const quint64 revisionBefore = fixture.viewModel->currentProjectRevision();
    const bool dirtyBefore = fixture.viewModel->state().saveState.isDirty;

    // Build real snapshots through the rendering pipeline, then corrupt one page's
    // source snapshot. The corruption passes UseCase pre-validation but is rejected
    // by the exporter as a page-level failure (source must be RGBA8888).
    ExportSnapshotBuilder builder(project, fixture.stack.pageImageLoader.get(), fixture.stack.derivedAssetStore.get());
    ExportSnapshotBuilder::Request snapshotRequest;
    snapshotRequest.projectRoot = fixture.projectRoot;
    auto snapshotFuture = builder.execute(snapshotRequest, OperationContext{});
    QVERIFY(awaitFuture(snapshotFuture));
    QVERIFY(snapshotFuture.result().has_value());
    auto snapshots = snapshotFuture.result().value();
    QCOMPARE(snapshots.size(), 3);
    snapshots[1].sourceImage = snapshots[1].sourceImage.convertToFormat(QImage::Format_ARGB32);

    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());
    ExportPagesUseCase exportUseCase(fixture.stack.exporter.get(), project);
    ExportPagesRequest request{
        .projectId = project->id(),
        .scope = ExportScope::AllPages,
        .outputDir = outputDir.path(),
        .format = ExportFormat::Png,
        .pageSnapshots = snapshots,
    };
    auto exportFuture = exportUseCase.execute(request, OperationContext{});
    QVERIFY(awaitFuture(exportFuture));
    QVERIFY(exportFuture.result().has_value());
    const auto response = exportFuture.result().value();

    // The corrupt page fails with an explicit page-level error; the other pages export.
    QCOMPARE(response.failures.size(), 1);
    QCOMPARE(response.failures.first().pageId, fixture.pageIds.at(1));
    QCOMPARE(response.failures.first().error.code, QStringLiteral("export.invalid_layer"));
    QCOMPARE(response.exported.size(), 2);
    QCOMPARE(response.exported[0].pageId, fixture.pageIds.at(0));
    QCOMPARE(response.exported[1].pageId, fixture.pageIds.at(2));
    QCOMPARE(fixture.pngFileNames(outputDir.path()).size(), 2);

    // The project content is untouched by the failed page.
    QCOMPARE(fixture.viewModel->currentProjectRevision(), revisionBefore);
    QCOMPARE(fixture.viewModel->state().saveState.isDirty, dirtyBefore);

    // Corrupting the source reference itself (deleted original asset) fails the whole
    // ViewModel export without producing partial output or touching the project.
    const auto* brokenPage = project->findPage(fixture.pageIds.at(1));
    QVERIFY(brokenPage != nullptr);
    const QString brokenAsset =
        QDir(fixture.projectRoot).filePath(QString::fromStdString(brokenPage->originalAssetPath()));
    QVERIFY(QFile::exists(brokenAsset));
    QVERIFY(QFile::remove(brokenAsset));

    QTemporaryDir secondOutputDir;
    QVERIFY(secondOutputDir.isValid());
    QSignalSpy completedSpy(fixture.viewModel.get(), &EditorViewModel::exportCompleted);
    QSignalSpy failedSpy(fixture.viewModel.get(), &EditorViewModel::exportFailed);
    fixture.viewModel->exportPages(ExportScope::AllPages, secondOutputDir.path());
    QVERIFY(failedSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(completedSpy.count(), 0);
    QVERIFY(!fixture.viewModel->state().isExporting);
    QVERIFY(fixture.viewModel->state().notice.has_value());
    QCOMPARE(fixture.pngFileNames(secondOutputDir.path()).size(), 0);
    QCOMPARE(fixture.viewModel->currentProjectRevision(), revisionBefore);
    QCOMPARE(fixture.viewModel->state().saveState.isDirty, dirtyBefore);
}

QTEST_MAIN(PageExportScenarioTest)
#include "PageExportScenarioTest.moc"
