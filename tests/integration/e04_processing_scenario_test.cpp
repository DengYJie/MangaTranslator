#include "FakeOcrEngine.h"
#include "FakeTextLayouter.h"
#include "FakeTranslator.h"
#include "IntegrationHarness.h"
#include "application/dto/ExportDto.h"
#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "rendering/TextLayoutEngine.h"
#include "rendering/TextRasterizer.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QSignalSpy>
#include <QThreadPool>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

/// Documented pixel tolerance for sRGB round-trips through the PNG encoder.
constexpr int kChannelTolerance = 2;

/// Digests one file so its content can be matched against a sha256Tree value set.
[[nodiscard]] QString fileDigest(const QString& absolutePath) {
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

/// Counting decorator over the real derived asset store. Undo and Redo must switch
/// references only, so the counters stay at zero while the undo stack moves.
class CountingDerivedAssetStore : public IDerivedAssetStore {
public:
    explicit CountingDerivedAssetStore(IDerivedAssetStore* inner) : m_inner(inner) {}

    [[nodiscard]] QFuture<Result<QImage>> readDerivedLayer(ReadDerivedLayerRequest request,
                                                           OperationContext context) override {
        ++readCount;
        return m_inner->readDerivedLayer(std::move(request), context);
    }

    [[nodiscard]] QFuture<Result<CommitDerivedLayerResponse>> commitDerivedLayer(CommitDerivedLayerRequest request,
                                                                                 OperationContext context) override {
        ++commitCount;
        return m_inner->commitDerivedLayer(std::move(request), context);
    }

    int readCount{0};
    int commitCount{0};

private:
    IDerivedAssetStore* m_inner; // non-owning
};

struct ImageDiff {
    int maxChannelDelta{0};
    qsizetype differingPixels{0};
};

/// Compares two images channel by channel after normalizing to straight RGBA8888.
/// A size or null mismatch reports the maximum possible delta.
[[nodiscard]] ImageDiff diffImages(const QImage& lhs, const QImage& rhs, int tolerance) {
    ImageDiff diff;
    if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size()) {
        diff.maxChannelDelta = 255;
        diff.differingPixels = -1;
        return diff;
    }
    const QImage left = lhs.convertToFormat(QImage::Format_RGBA8888);
    const QImage right = rhs.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const QColor a = left.pixelColor(x, y);
            const QColor b = right.pixelColor(x, y);
            const int delta = std::max({std::abs(a.red() - b.red()),
                                        std::abs(a.green() - b.green()),
                                        std::abs(a.blue() - b.blue()),
                                        std::abs(a.alpha() - b.alpha())});
            diff.maxChannelDelta = std::max(diff.maxChannelDelta, delta);
            if (delta > tolerance) {
                ++diff.differingPixels;
            }
        }
    }
    return diff;
}

/// Reproduces the translation layer rendering of ExportSnapshotBuilder: the canvas
/// preview and the export path share these two rendering entry points, so feeding them
/// the same region geometry, layout and DPI must yield identical pixels.
[[nodiscard]] QImage renderTranslationLayer(const domain::ComicPage& page, const QSize& pageSize, double logicalDpi) {
    QImage layer(pageSize, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);

    bool hasText = false;
    for (const auto& region : page.regions()) {
        if (region.isEnabled() && region.isTranslationVisible() && !region.translation().effectiveText().empty()) {
            hasText = true;
            break;
        }
    }
    if (!hasText) {
        return QImage();
    }

    const double dpm = logicalDpi * 1000.0 / 25.4;
    layer.setDotsPerMeterX(qRound(dpm));
    layer.setDotsPerMeterY(qRound(dpm));

    QPainter painter(&layer);
    for (const auto& region : page.regions()) {
        if (!region.isEnabled() || !region.isTranslationVisible()) {
            continue;
        }
        const QString text = QString::fromStdString(region.translation().effectiveText());
        if (text.isEmpty()) {
            continue;
        }
        const auto layout =
            rendering::TextLayoutEngine::layoutText(text, region.geometry(), region.layout(), logicalDpi);
        rendering::TextRasterizer::draw(painter, layout, logicalDpi);
    }
    painter.end();

    QImage result = layer.convertToFormat(QImage::Format_RGBA8888);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return result;
}

/// Composites the four export layers in the documented order:
/// Original -> Clear -> Translation -> ManualOverride.
[[nodiscard]] QImage
compositeLayers(const QImage& source, const QImage& clear, const QImage& translation, const QImage& manualOverride) {
    QImage composite(source.size(), QImage::Format_RGBA8888_Premultiplied);
    composite.setColorSpace(QColorSpace(QColorSpace::SRgb));
    composite.fill(Qt::transparent);
    {
        QPainter painter(&composite);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, source);
        if (!clear.isNull()) {
            painter.drawImage(0, 0, clear);
        }
        if (!translation.isNull()) {
            painter.drawImage(0, 0, translation);
        }
        if (!manualOverride.isNull()) {
            painter.drawImage(0, 0, manualOverride);
        }
    }
    QImage result = composite.convertToFormat(QImage::Format_RGBA8888);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return result;
}

/// Finds the first opaque glyph pixel with dark ink so layer order probes never land on
/// an anti-aliased edge or on empty space.
[[nodiscard]] std::optional<QPoint> findInkPixel(const QImage& layer) {
    const QImage image = layer.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 200) {
                continue;
            }
            if (std::max({pixel.red(), pixel.green(), pixel.blue()}) < 128) {
                return QPoint(x, y);
            }
        }
    }
    return std::nullopt;
}

struct ProcessingFixture {
    RealDataStack stack;
    QString projectRoot;
    domain::ComicProject project{"ProcessingScenario"};
    std::unique_ptr<manga::application::test::FakeOcrEngine> ocrEngine;
    std::unique_ptr<manga::application::test::FakeTranslator> translator;
    std::unique_ptr<manga::application::test::FakeTextLayouter> fakeLayouter;
    std::unique_ptr<CountingDerivedAssetStore> countingStore;
    std::unique_ptr<EditorViewModel> viewModel;
    std::unique_ptr<EditorProcessingController> controller;
    domain::PageId pageId;
    domain::RegionId regionId;

    bool init(const QString& scenarioName, bool useFakeLayouter, bool useCountingStore) {
        projectRoot = stack.projectRoot(scenarioName);
        ocrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();
        translator = std::make_unique<manga::application::test::FakeTranslator>();
        fakeLayouter = std::make_unique<manga::application::test::FakeTextLayouter>();
        if (useCountingStore) {
            countingStore = std::make_unique<CountingDerivedAssetStore>(stack.derivedAssetStore.get());
        }

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
        project = openFuture.result().value().project;

        const QString page1Path =
            QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_001.png"));
        if (!QFileInfo::exists(page1Path)) {
            return false;
        }

        ImportImagesRequest importReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .filePaths = {page1Path},
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
        if (!applyRes.has_value() || applyRes.value().empty()) {
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

        viewModel = std::make_unique<EditorViewModel>(
            stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());

        OpenedProjectSession session{
            .project = project,
            .projectRootPath = projectRoot,
            .recoverySource = RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(session);
        if (!viewModel->hasSession() || viewModel->state().pages.isEmpty()) {
            return false;
        }

        pageId = viewModel->state().pages.first().id;
        QSignalSpy pageImageSpy(viewModel.get(), &EditorViewModel::pageImageReady);
        viewModel->selectPage(pageId);
        if (viewModel->currentPageImage().isNull() && !pageImageSpy.wait(kAwaitTimeoutMs)) {
            return false;
        }
        if (viewModel->currentPageImage().isNull()) {
            return false;
        }

        viewModel->createRegion(domain::RectGeometry{50.0, 50.0, 160.0, 100.0});
        if (viewModel->state().regions.isEmpty()) {
            return false;
        }
        regionId = viewModel->state().regions.first().id;

        auto* layouter = useFakeLayouter ? static_cast<ITextLayouter*>(fakeLayouter.get())
                                         : static_cast<ITextLayouter*>(stack.layouter.get());
        auto* store = useCountingStore ? static_cast<IDerivedAssetStore*>(countingStore.get())
                                       : static_cast<IDerivedAssetStore*>(stack.derivedAssetStore.get());

        controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                  ocrEngine.get(),
                                                                  translator.get(),
                                                                  stack.inpainter.get(),
                                                                  layouter,
                                                                  store,
                                                                  &stack.operationRegistry);
        return true;
    }

    /// Runs the full OCR -> translation -> clear -> layout pipeline and adopts the proposal.
    [[nodiscard]] bool runFullPipeline() {
        QSignalSpy proposalSpy(controller.get(), &EditorProcessingController::proposalReady);
        controller->smartTranslatePage(pageId);
        if (!proposalSpy.wait(kAwaitTimeoutMs)) {
            return false;
        }
        if (!controller->hasProposal(pageId) || controller->preparedAdoption(pageId) == nullptr) {
            return false;
        }
        controller->adoptProposal(pageId);
        return !controller->hasProposal(pageId);
    }
};

/// Saves the session, closes it and reopens the project through the repository.
[[nodiscard]] domain::ComicProject saveCloseReopen(ProcessingFixture& fixture) {
    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    if (!saveSpy.wait(kAwaitTimeoutMs)) {
        return {};
    }
    QSignalSpy closeSpy(fixture.viewModel.get(), &EditorViewModel::projectClosed);
    fixture.viewModel->requestCloseProject();
    if (closeSpy.count() != 1) {
        return {};
    }
    // The close is a queued data operation; drain it so the reopen cannot race it.
    QThreadPool::globalInstance()->waitForDone();

    OpenProjectRequest openReq{.path = fixture.projectRoot};
    auto openFuture = OpenProjectUseCase(fixture.stack.repository.get()).execute(openReq, OperationContext{});
    if (!awaitFuture(openFuture) || !openFuture.result().has_value()) {
        return {};
    }
    return openFuture.result().value().project;
}

} // namespace

class ProcessingScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void fullProcessingMainlineProducesConsistentExport();
    void clearUndoRedoOnlySwitchesReferencesWithoutIo();
    void previewRasterizationMatchesExportWithinTolerance();
    void failedStageKeepsCommittedStagesAndOriginals();
};

void ProcessingScenarioTest::initTestCase() {
    qRegisterMetaType<domain::PageId>();
    qRegisterMetaType<domain::RegionId>();
    qRegisterMetaType<application::OperationId>();
    qRegisterMetaType<ui::screen::editor::EditorState>();
}

void ProcessingScenarioTest::fullProcessingMainlineProducesConsistentExport() {
    ProcessingFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e04_full_mainline"), false, false));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    const QString recognizedText = QStringLiteral("漫画のテスト文字");
    fixture.ocrEngine->nextResponse.text = recognizedText;
    fixture.ocrEngine->nextResponse.confidence = 0.97f;
    fixture.translator->nextResponse.providerId = QStringLiteral("fake-translator");

    const auto originalHashes = sha256Tree(fixture.projectRoot, QStringLiteral("assets/original"));
    QCOMPARE(originalHashes.size(), 1);

    const int initialUndoCount = fixture.viewModel->undoStackCountForTest();
    const domain::RectGeometry regionGeometry =
        fixture.viewModel->projectAggregate()->findPage(pageId)->findRegion(regionId)->geometry();

    // The pipeline result is only a proposal: the clear asset is committed during prepare
    // while the Domain still holds the pre-processing state.
    QSignalSpy proposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->smartTranslatePage(pageId);
    QVERIFY(proposalSpy.wait(kAwaitTimeoutMs));

    const auto* proposal = fixture.controller->proposal(pageId);
    QVERIFY(proposal != nullptr);
    QCOMPARE(proposal->ocr.size(), 1);
    QCOMPARE(proposal->ocr.first().text, recognizedText);
    QVERIFY(proposal->translation.has_value());
    QCOMPARE(proposal->translation->items.size(), 1);
    QCOMPARE(proposal->clear.size(), 1);
    QCOMPARE(proposal->layout.size(), 1);
    QVERIFY(proposal->issues.isEmpty());

    const auto* prepared = fixture.controller->preparedAdoption(pageId);
    QVERIFY(prepared != nullptr);
    QVERIFY(prepared->clearAssetChange.has_value());
    const QString clearRef = prepared->clearAssetChange->afterRef;
    QVERIFY(!clearRef.isEmpty());
    QVERIFY(QFileInfo::exists(QDir(fixture.projectRoot).filePath(clearRef)));

    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);
    QVERIFY(!project->findPage(pageId)->clearLayerAsset().has_value());
    QVERIFY(!project->findPage(pageId)->findRegion(regionId)->isCleared());

    QSignalSpy adoptSpy(fixture.controller.get(), &EditorProcessingController::proposalAdopted);
    fixture.controller->adoptProposal(pageId);
    QCOMPARE(adoptSpy.count(), 1);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), initialUndoCount + 1);

    const auto* adoptedRegion = project->findPage(pageId)->findRegion(regionId);
    QCOMPARE(QString::fromStdString(adoptedRegion->source().text), recognizedText);
    QCOMPARE(adoptedRegion->translation().effectiveText(), std::string("translated"));
    QVERIFY(adoptedRegion->isCleared());
    QVERIFY(project->findPage(pageId)->clearLayerAsset().has_value());
    QCOMPARE(QString::fromStdString(*project->findPage(pageId)->clearLayerAsset()), clearRef);
    const auto expectedLayout = rendering::TextLayoutEngine::layoutText(
                                    QStringLiteral("translated"), regionGeometry, domain::TextLayout{}, 96.0)
                                    .resolvedLayout;
    QCOMPARE(adoptedRegion->layout(), expectedLayout);

    const domain::ComicProject savedSnapshot = *project;

    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    // A successful save captures the revision and keeps the undo history intact.
    QVERIFY(fixture.viewModel->state().canUndo);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), initialUndoCount + 1);

    QSignalSpy closeSpy(fixture.viewModel.get(), &EditorViewModel::projectClosed);
    fixture.viewModel->requestCloseProject();
    QVERIFY(closeSpy.wait(kAwaitTimeoutMs));
    // The close is a queued data operation; drain it so the reopen cannot race it.
    QThreadPool::globalInstance()->waitForDone();

    OpenProjectRequest openReq{.path = fixture.projectRoot};
    auto openFuture = OpenProjectUseCase(fixture.stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    QVERIFY(openFuture.result().has_value());
    domain::ComicProject reopened = openFuture.result().value().project;
    QVERIFY(reopened == savedSnapshot);

    const auto* reopenedPage = reopened.findPage(pageId);
    QVERIFY(reopenedPage != nullptr);
    QVERIFY(reopenedPage->clearLayerAsset().has_value());
    const QString persistedRef = QString::fromStdString(*reopenedPage->clearLayerAsset());
    QCOMPARE(persistedRef, clearRef);
    QVERIFY(QFileInfo::exists(QDir(fixture.projectRoot).filePath(persistedRef)));

    // The stored layer backs the reference: reading it yields a usable page-sized sRGB layer.
    ReadDerivedLayerRequest readReq{
        .projectId = reopened.id(),
        .projectRoot = fixture.projectRoot,
        .pageId = pageId,
        .layerKind = DerivedLayerKind::Clear,
        .relativePath = persistedRef,
    };
    auto readFuture = fixture.stack.derivedAssetStore->readDerivedLayer(readReq, OperationContext{});
    QVERIFY(awaitFuture(readFuture));
    QVERIFY(readFuture.result().has_value());
    const QImage storedClear = readFuture.result().value();
    QCOMPARE(storedClear.size(), QSize(reopenedPage->imageSize().width, reopenedPage->imageSize().height));
    // The PNG round-trip decodes as ARGB32_Premultiplied; consumers normalize to straight
    // RGBA8888 before compositing.
    QCOMPARE(storedClear.convertToFormat(QImage::Format_RGBA8888).format(), QImage::Format_RGBA8888);

    ExportSnapshotBuilder snapshotBuilder(
        &reopened, fixture.stack.pageImageLoader.get(), fixture.stack.derivedAssetStore.get());
    ExportSnapshotBuilder::Request snapReq{
        .projectRoot = fixture.projectRoot,
        .pageIds = {},
        .logicalDpi = 96.0,
    };
    auto snapFuture = snapshotBuilder.execute(snapReq, OperationContext{});
    QVERIFY(awaitFuture(snapFuture));
    QVERIFY(snapFuture.result().has_value());
    const auto snapshots = snapFuture.result().value();
    QCOMPARE(snapshots.size(), 1);

    const auto& snapshot = snapshots.first();
    const QSize pageSize(reopenedPage->imageSize().width, reopenedPage->imageSize().height);
    QVERIFY(!snapshot.sourceImage.isNull());
    QCOMPARE(snapshot.sourceImage.size(), pageSize);
    QCOMPARE(snapshot.sourceImage.format(), QImage::Format_RGBA8888);
    QVERIFY(!snapshot.clearLayer.isNull());
    QCOMPARE(snapshot.clearLayer.size(), pageSize);
    QCOMPARE(snapshot.clearLayer.format(), QImage::Format_RGBA8888);
    QVERIFY(!snapshot.translationLayer.isNull());
    QCOMPARE(snapshot.translationLayer.size(), pageSize);
    QCOMPARE(snapshot.translationLayer.format(), QImage::Format_RGBA8888);
    QVERIFY(snapshot.manualOverrideLayer.isNull());

    const ImageDiff clearDiff = diffImages(storedClear, snapshot.clearLayer, kChannelTolerance);
    QVERIFY2(clearDiff.maxChannelDelta == 0 && clearDiff.differingPixels == 0,
             qPrintable(QStringLiteral("stored clear layer differs from snapshot: max=%1 pixels=%2")
                            .arg(clearDiff.maxChannelDelta)
                            .arg(clearDiff.differingPixels)));

    QTemporaryDir exportDir;
    QVERIFY(exportDir.isValid());
    ExportPagesUseCase exportUseCase(fixture.stack.exporter.get(), &reopened);
    ExportPagesRequest pngReq{
        .projectId = reopened.id(),
        .scope = ExportScope::AllPages,
        .outputDir = exportDir.path(),
        .format = ExportFormat::Png,
        .pageSnapshots = snapshots,
    };
    auto pngFuture = exportUseCase.execute(pngReq, OperationContext{});
    QVERIFY(awaitFuture(pngFuture));
    QVERIFY(pngFuture.result().has_value());
    const auto pngRes = pngFuture.result().value();
    QCOMPARE(pngRes.failures.size(), 0);
    QCOMPARE(pngRes.exported.size(), 1);
    QVERIFY(QFileInfo::exists(pngRes.exported.first().path));
    QVERIFY(QFileInfo(pngRes.exported.first().path).size() > 0);

    QImage exportedImage(pngRes.exported.first().path);
    QVERIFY(!exportedImage.isNull());
    QCOMPARE(exportedImage.size(), pageSize);

    const QImage expectedComposite =
        compositeLayers(snapshot.sourceImage, snapshot.clearLayer, snapshot.translationLayer, QImage());
    const ImageDiff exportDiff = diffImages(expectedComposite, exportedImage, kChannelTolerance);
    QVERIFY2(exportDiff.maxChannelDelta <= kChannelTolerance,
             qPrintable(QStringLiteral("exported PNG differs from layered composite: max=%1 pixels=%2")
                            .arg(exportDiff.maxChannelDelta)
                            .arg(exportDiff.differingPixels)));

    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/original")), originalHashes);
}

void ProcessingScenarioTest::clearUndoRedoOnlySwitchesReferencesWithoutIo() {
    ProcessingFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e04_clear_undo_redo"), false, true));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    QSignalSpy proposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->clearRegion(pageId, regionId);
    QVERIFY(proposalSpy.wait(kAwaitTimeoutMs));

    // Prepare commits the immutable asset before anything is adopted. A first-time clear
    // has no previous layer to read, so the read counter stays at zero.
    QVERIFY(fixture.countingStore->commitCount >= 1);
    QCOMPARE(fixture.countingStore->readCount, 0);

    const auto* prepared = fixture.controller->preparedAdoption(pageId);
    QVERIFY(prepared != nullptr);
    QVERIFY(prepared->clearAssetChange.has_value());
    const QString afterRef = prepared->clearAssetChange->afterRef;
    const QString beforeRef = prepared->clearAssetChange->beforeRef;
    QVERIFY(!afterRef.isEmpty());
    QVERIFY(beforeRef.isEmpty());

    QSignalSpy adoptSpy(fixture.controller.get(), &EditorProcessingController::proposalAdopted);
    fixture.controller->adoptProposal(pageId);
    QCOMPARE(adoptSpy.count(), 1);

    const auto* clearedPage = project->findPage(pageId);
    QVERIFY(clearedPage->clearLayerAsset().has_value());
    QCOMPARE(QString::fromStdString(*clearedPage->clearLayerAsset()), afterRef);
    QVERIFY(clearedPage->findRegion(regionId)->isCleared());

    fixture.countingStore->readCount = 0;
    fixture.countingStore->commitCount = 0;
    const auto derivedBefore = sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived"));

    QVERIFY(fixture.viewModel->state().canUndo);
    fixture.viewModel->undo();

    QCOMPARE(fixture.countingStore->readCount, 0);
    QCOMPARE(fixture.countingStore->commitCount, 0);
    const auto* undonePage = project->findPage(pageId);
    QVERIFY(!undonePage->clearLayerAsset().has_value());
    QVERIFY(!undonePage->findRegion(regionId)->isCleared());
    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived")), derivedBefore);

    QVERIFY(fixture.viewModel->state().canRedo);
    fixture.viewModel->redo();

    QCOMPARE(fixture.countingStore->readCount, 0);
    QCOMPARE(fixture.countingStore->commitCount, 0);
    const auto* redonePage = project->findPage(pageId);
    QVERIFY(redonePage->clearLayerAsset().has_value());
    QCOMPARE(QString::fromStdString(*redonePage->clearLayerAsset()), afterRef);
    QVERIFY(redonePage->findRegion(regionId)->isCleared());
    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived")), derivedBefore);

    // The committed asset is still readable, so the reference switch is lossless.
    ReadDerivedLayerRequest readReq{
        .projectId = project->id(),
        .projectRoot = fixture.projectRoot,
        .pageId = pageId,
        .layerKind = DerivedLayerKind::Clear,
        .relativePath = afterRef,
    };
    auto readFuture = fixture.stack.derivedAssetStore->readDerivedLayer(readReq, OperationContext{});
    QVERIFY(awaitFuture(readFuture));
    QVERIFY(readFuture.result().has_value());
    QVERIFY(!readFuture.result().value().isNull());
}

void ProcessingScenarioTest::previewRasterizationMatchesExportWithinTolerance() {
    ProcessingFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e04_preview_export"), false, false));

    const auto pageId = fixture.pageId;
    fixture.ocrEngine->nextResponse.text = QStringLiteral("プレビュー原文");
    fixture.ocrEngine->nextResponse.confidence = 0.93f;

    QVERIFY(fixture.runFullPipeline());
    domain::ComicProject reopened = saveCloseReopen(fixture);
    QVERIFY(reopened.findPage(pageId) != nullptr);

    const auto* page = reopened.findPage(pageId);
    const QSize pageSize(page->imageSize().width, page->imageSize().height);

    ExportSnapshotBuilder snapshotBuilder(
        &reopened, fixture.stack.pageImageLoader.get(), fixture.stack.derivedAssetStore.get());
    ExportSnapshotBuilder::Request snapReq{
        .projectRoot = fixture.projectRoot,
        .pageIds = {pageId},
        .logicalDpi = 96.0,
    };
    auto snapFuture = snapshotBuilder.execute(snapReq, OperationContext{});
    QVERIFY(awaitFuture(snapFuture));
    QVERIFY(snapFuture.result().has_value());
    const auto snapshots = snapFuture.result().value();
    QCOMPARE(snapshots.size(), 1);
    const auto& snapshot = snapshots.first();
    QVERIFY(!snapshot.translationLayer.isNull());

    // Same rendering engine, same inputs: the preview raster and the export raster are identical.
    const QImage previewTranslation = renderTranslationLayer(*page, pageSize, 96.0);
    QVERIFY(!previewTranslation.isNull());
    const ImageDiff translationDiff = diffImages(previewTranslation, snapshot.translationLayer, 0);
    QVERIFY2(translationDiff.maxChannelDelta == 0 && translationDiff.differingPixels == 0,
             qPrintable(QStringLiteral("preview raster differs from export raster: max=%1 pixels=%2")
                            .arg(translationDiff.maxChannelDelta)
                            .arg(translationDiff.differingPixels)));

    const QImage previewComposite = compositeLayers(
        snapshot.sourceImage, snapshot.clearLayer, snapshot.translationLayer, snapshot.manualOverrideLayer);

    QTemporaryDir exportDir;
    QVERIFY(exportDir.isValid());
    ExportPagesUseCase exportUseCase(fixture.stack.exporter.get(), &reopened);
    ExportPagesRequest pngReq{
        .projectId = reopened.id(),
        .scope = ExportScope::AllPages,
        .outputDir = exportDir.path(),
        .format = ExportFormat::Png,
        .pageSnapshots = snapshots,
    };
    auto pngFuture = exportUseCase.execute(pngReq, OperationContext{});
    QVERIFY(awaitFuture(pngFuture));
    QVERIFY(pngFuture.result().has_value());
    QCOMPARE(pngFuture.result().value().failures.size(), 0);
    QCOMPARE(pngFuture.result().value().exported.size(), 1);

    QImage exportedImage(pngFuture.result().value().exported.first().path);
    QVERIFY(!exportedImage.isNull());
    QCOMPARE(exportedImage.size(), pageSize);

    const ImageDiff compositeDiff = diffImages(previewComposite, exportedImage, kChannelTolerance);
    QVERIFY2(compositeDiff.maxChannelDelta <= kChannelTolerance,
             qPrintable(QStringLiteral("exported PNG differs from preview composite: max=%1 pixels=%2")
                            .arg(compositeDiff.maxChannelDelta)
                            .arg(compositeDiff.differingPixels)));

    // Layer order: the translation layer must sit above the opaque clear layer. Drawing the
    // clear layer last would erase the glyphs, so the swapped composite has to disagree.
    const auto ink = findInkPixel(snapshot.translationLayer);
    QVERIFY(ink.has_value());
    const auto& inkBounds = page->regions().front().geometry();
    QVERIFY(ink->x() >= inkBounds.x && ink->x() <= inkBounds.x + inkBounds.width);
    QVERIFY(ink->y() >= inkBounds.y && ink->y() <= inkBounds.y + inkBounds.height);

    const QImage swapped = compositeLayers(
        snapshot.sourceImage, snapshot.translationLayer, snapshot.clearLayer, snapshot.manualOverrideLayer);
    const QColor exportedInk = exportedImage.convertToFormat(QImage::Format_RGBA8888).pixelColor(*ink);
    const QColor swappedInk = swapped.pixelColor(*ink);
    QVERIFY2(
        std::abs(exportedInk.red() - swappedInk.red()) > 64 ||
            std::abs(exportedInk.green() - swappedInk.green()) > 64 ||
            std::abs(exportedInk.blue() - swappedInk.blue()) > 64,
        qPrintable(
            QStringLiteral("translation layer is not above the clear layer at %1,%2").arg(ink->x()).arg(ink->y())));

    // Alpha and text position: the glyph pixel is opaque in both the export and the preview.
    const QColor previewInk = previewComposite.pixelColor(*ink);
    QCOMPARE(exportedInk.alpha(), 255);
    QCOMPARE(previewInk.alpha(), 255);
    QCOMPARE(exportedInk, previewInk);
}

void ProcessingScenarioTest::failedStageKeepsCommittedStagesAndOriginals() {
    ProcessingFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("e04_layout_failure"), true, false));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    fixture.ocrEngine->nextResponse.text = QStringLiteral("失敗テスト原文");
    fixture.ocrEngine->nextResponse.confidence = 0.91f;

    // Committed stage: the clear asset is adopted and referenced before the failure is injected.
    QSignalSpy clearProposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->clearRegion(pageId, regionId);
    QVERIFY(clearProposalSpy.wait(kAwaitTimeoutMs));
    fixture.controller->adoptProposal(pageId);

    const auto* clearedPage = project->findPage(pageId);
    QVERIFY(clearedPage->clearLayerAsset().has_value());
    const QString committedClearRef = QString::fromStdString(*clearedPage->clearLayerAsset());
    const auto derivedBefore = sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived"));
    const auto originalBefore = sha256Tree(fixture.projectRoot, QStringLiteral("assets/original"));
    const auto sourceBefore = clearedPage->findRegion(regionId)->source();
    const auto translationBefore = clearedPage->findRegion(regionId)->translation();
    const auto revisionBefore = clearedPage->findRegion(regionId)->contentRevision();
    QVERIFY(clearedPage->findRegion(regionId)->isCleared());
    QVERIFY(derivedBefore.values().contains(fileDigest(QDir(fixture.projectRoot).filePath(committedClearRef))));

    // Inject the failure at the layout stage of the full pipeline.
    const QString safeSummary = QStringLiteral("The text layout engine is unavailable");
    fixture.fakeLayouter->shouldFail = true;
    fixture.fakeLayouter->nextError =
        makeApplicationError(ErrorCategory::Internal, QStringLiteral("layout.engine_unavailable"), safeSummary);

    QSignalSpy startSpy(fixture.controller.get(), &EditorProcessingController::processingStarted);
    QSignalSpy failProposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->smartTranslatePage(pageId);
    QVERIFY(failProposalSpy.wait(kAwaitTimeoutMs));
    QVERIFY(startSpy.count() >= 1);
    const auto failedOperationId = startSpy.first().at(1).value<application::OperationId>();
    QVERIFY(failedOperationId.isValid());

    const auto* failedProposal = fixture.controller->proposal(pageId);
    QVERIFY(failedProposal != nullptr);
    bool hasLayoutIssue = false;
    for (const auto& issue : failedProposal->issues) {
        if (issue.stage == QStringLiteral("Layout") && issue.regionId == std::optional<domain::RegionId>(regionId) &&
            issue.error.safeMessage == safeSummary) {
            hasLayoutIssue = true;
        }
        // A safe summary never leaks provider or filesystem details.
        QVERIFY(!issue.error.safeMessage.contains(fixture.projectRoot));
    }
    QVERIFY(hasLayoutIssue);
    QVERIFY(failedProposal->layout.isEmpty());
    QCOMPARE(failedProposal->clear.size(), 1);

    // The failed stage is never adopted, so the committed stages stay untouched.
    const auto* preservedPage = project->findPage(pageId);
    QVERIFY(preservedPage->clearLayerAsset().has_value());
    QCOMPARE(QString::fromStdString(*preservedPage->clearLayerAsset()), committedClearRef);
    QVERIFY(preservedPage->findRegion(regionId)->isCleared());
    QCOMPARE(preservedPage->findRegion(regionId)->source(), sourceBefore);
    QCOMPARE(preservedPage->findRegion(regionId)->translation(), translationBefore);
    QCOMPARE(preservedPage->findRegion(regionId)->contentRevision(), revisionBefore);

    // No half-finished artifact: the derived and original trees are byte-identical, and the
    // prepared clear reuse keeps the content-addressed file set unchanged.
    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived")), derivedBefore);
    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/original")), originalBefore);

    // Retry the failed stage under a new operation id.
    fixture.fakeLayouter->shouldFail = false;
    fixture.fakeLayouter->nextError = ApplicationError{};
    domain::TextLayout retryLayout;
    retryLayout.fontSizePt = 18.0;
    fixture.fakeLayouter->nextResponse = LayoutResponse{};
    fixture.fakeLayouter->nextResponse.resolvedLayout = retryLayout;

    QSignalSpy retryStartSpy(fixture.controller.get(), &EditorProcessingController::processingStarted);
    QSignalSpy retryProposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->retryFailedStage(pageId, regionId, OperationStage::Layout);
    QVERIFY(retryProposalSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(retryStartSpy.count(), 1);
    const auto retryOperationId = retryStartSpy.first().at(1).value<application::OperationId>();
    QVERIFY(retryOperationId.isValid());
    QVERIFY(!(retryOperationId == failedOperationId));

    const int undoBeforeRetry = fixture.viewModel->undoStackCountForTest();
    fixture.controller->adoptProposal(pageId);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), undoBeforeRetry + 1);
    QVERIFY(project->findPage(pageId)->findRegion(regionId)->layout().fontSizePt == retryLayout.fontSizePt);
    QCOMPARE(sha256Tree(fixture.projectRoot, QStringLiteral("assets/original")), originalBefore);
}

QTEST_MAIN(ProcessingScenarioTest)
#include "e04_processing_scenario_test.moc"
