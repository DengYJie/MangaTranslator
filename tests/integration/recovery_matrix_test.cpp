#include "FakeCredentialStore.h"
#include "FakeOcrEngine.h"
#include "FakeTranslator.h"
#include "IntegrationHarness.h"
#include "LoopbackHttpServer.h"
#include "application/dto/ClearDto.h"
#include "application/dto/ImportDto.h"
#include "application/dto/LayoutDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/ProjectDto.h"
#include "application/dto/TranslationDto.h"
#include "application/ports/IComicExporter.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/TextContent.h"
#include "domain/model/TranslationEntry.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"
#include "network/translation/RetryPolicy.h"
#include "services/ocr/OcrFactory.h"
#include "ui/error/RecoveryPresenter.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::error;

class ChoosePathFailingExporter final : public IComicExporter {
public:
    QFuture<Result<ExportResponse>> exportPages(ExportRequest, OperationContext) override {
        return makeFailedFuture<ExportResponse>(
            makeApplicationError(ErrorCategory::Persistence,
                                 QStringLiteral("export.output_unavailable"),
                                 QStringLiteral("The export destination is unavailable"),
                                 RecoveryAction::ChoosePath));
    }
};
using namespace manga::ui::manga;
using namespace manga::ui::screen::editor;

namespace {

struct RecoveryFixture {
    RealDataStack stack;
    QString projectRoot;
    domain::ComicProject project{"RecoveryMatrixTestProject"};
    std::unique_ptr<manga::application::test::FakeOcrEngine> fakeOcrEngine;
    std::unique_ptr<manga::application::test::FakeTranslator> fakeTranslator;
    std::unique_ptr<EditorViewModel> viewModel;
    std::unique_ptr<EditorProcessingController> controller;
    QVector<domain::PageId> pageIds;

    [[nodiscard]] bool init(const QString& scenarioName, int pageCount = 1) {
        projectRoot = stack.projectRoot(scenarioName);
        fakeOcrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();
        fakeTranslator = std::make_unique<manga::application::test::FakeTranslator>();

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

        QStringList paths;
        for (int i = 1; i <= pageCount; ++i) {
            const QString p =
                QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_00%1.png").arg(i));
            if (!QFileInfo::exists(p)) {
                return false;
            }
            paths.append(p);
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

        viewModel = std::make_unique<EditorViewModel>(
            stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
        viewModel->setExportDependencies(stack.exporter.get(), stack.derivedAssetStore.get());

        controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                  fakeOcrEngine.get(),
                                                                  fakeTranslator.get(),
                                                                  stack.inpainter.get(),
                                                                  nullptr,
                                                                  stack.layouter.get(),
                                                                  stack.derivedAssetStore.get(),
                                                                  &stack.operationRegistry);
        viewModel->setProcessingController(controller.get());

        OpenedProjectSession session{
            .project = std::move(project),
            .projectRootPath = projectRoot,
            .recoverySource = RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(std::move(session));
        return viewModel->hasSession();
    }
};

} // namespace

class RecoveryMatrixTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<domain::RegionId>();
        qRegisterMetaType<application::OperationId>();
        qRegisterMetaType<ProjectCloseSummary>();
        qRegisterMetaType<application::ApplicationError>();
        qRegisterMetaType<PageImageDelivery>();
    }

    void missingImageKeepsProjectAndPageSelection();
    void ocrModelMissingKeepsManualEditingSaveExport();
    void credentialMissingOr401KeepsManualTranslationAndCandidates();
    void rateLimitTimeoutOfflineKeepAdoptedResults();
    void clearLayoutFailureWritesNoPartialDerivedAsset();
    void saveFailureKeepsDirtyAndLastValidFile();
    void exportFailureKeepsProjectAndExistingOutputs();
    void projectSwitchRejectsLateResults();
};

void RecoveryMatrixTest::missingImageKeepsProjectAndPageSelection() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("missing_image_recovery"), 2));

    auto* vm = fixture.viewModel.get();
    EditorPage editorPage(vm);
    editorPage.show();

    const auto p1Id = fixture.pageIds[0];
    const auto p2Id = fixture.pageIds[1];

    // Page 1 is initial currentPageId and loads
    vm->selectPage(p1Id);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);
    QVERIFY(!vm->state().isPageLoading);
    QVERIFY(!vm->state().notice.has_value());
    QVERIFY(editorPage.canvasForTest()->hasPageImageForTest());
    QCOMPARE(editorPage.canvasForTest()->contentState(), MangaCanvas::ContentState::Image);

    // Delete page 2 original file on disk
    const QString p2OriginalRel = QString::fromStdString(vm->projectAggregate()->findPage(p2Id)->originalAssetPath());
    const QString p2OriginalAbs = QDir(fixture.projectRoot).filePath(p2OriginalRel);
    QVERIFY(QFileInfo::exists(p2OriginalAbs));

    const QString fixtureP2Path =
        QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_002.png"));
    QVERIFY(QFile::remove(p2OriginalAbs));
    QVERIFY(!QFileInfo::exists(p2OriginalAbs));

    // Select page 2
    vm->selectPage(p2Id);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);
    QVERIFY(!vm->state().isPageLoading);

    // Assert: project and page selection preserved
    QCOMPARE(vm->state().projectId, vm->projectAggregate()->id());
    QCOMPARE(vm->state().currentPageId, p2Id);

    // Assert: canvas cleared without stale images and error notice is set
    QVERIFY(vm->state().notice.has_value());
    QVERIFY(!vm->state().notice->safeMessage.isEmpty());
    QVERIFY(vm->state().notice->retryable);
    QCOMPARE(editorPage.canvasForTest()->contentState(), MangaCanvas::ContentState::Error);
    QVERIFY(!editorPage.canvasForTest()->hasPageImageForTest());

    // Restore missing file and retry
    QVERIFY(QFile::copy(fixtureP2Path, p2OriginalAbs));
    QVERIFY(QFileInfo::exists(p2OriginalAbs));

    vm->retryPageLoad();
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);
    QVERIFY(!vm->state().isPageLoading);
    QVERIFY(!vm->state().notice.has_value());
    QVERIFY(editorPage.canvasForTest()->hasPageImageForTest());
    QCOMPARE(editorPage.canvasForTest()->contentState(), MangaCanvas::ContentState::Image);

    vm->closeProjectSession();
}

void RecoveryMatrixTest::ocrModelMissingKeepsManualEditingSaveExport() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("ocr_model_missing_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const domain::RectGeometry geom{40.0, 40.0, 100.0, 50.0};
    vm->createRegion(geom);
    QCOMPARE(vm->state().regions.size(), 1);
    const auto regionId = vm->state().regions.first().id;

    // Real OnnxMangaOcrEngine pointing to non-existent model directory
    const QString nonExistentModelDir = fixture.stack.projectRoot(QStringLiteral("nonexistent_model_dir"));
    auto realOcrEngine = services::createOcrEngine(fixture.stack.executor.get(), nonExistentModelDir);

    OcrRequest ocrReq{
        .pageId = pageId,
        .regionId = regionId,
        .image = vm->currentPageImage(),
        .rect = geom,
        .language = domain::languages::Japanese,
    };
    auto ocrFuture = realOcrEngine->recognize(ocrReq, OperationContext{});
    QVERIFY(awaitFuture(ocrFuture));
    auto ocrRes = ocrFuture.result();
    QVERIFY(!ocrRes.has_value());

    const auto& error = ocrRes.error();
    QCOMPARE(error.category, ErrorCategory::Model);
    QCOMPARE(error.code, QStringLiteral("model.missing"));
    QCOMPARE(error.recovery, RecoveryAction::LocateModel);

    // Consume through RecoveryPresenter
    RecoveryPresenter presenter;
    const auto presentation = presenter.present(error);
    QVERIFY(presentation.has_value());
    QCOMPARE(presentation->action, RecoveryAction::LocateModel);
    QCOMPARE(presentation->actionText, QStringLiteral("选择模型目录"));
    QCOMPARE(RecoveryPresenter::settingsRouteForAction(presentation->action), QStringLiteral("settings/ocr"));

    // Manual editing continues normally
    vm->commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, QStringLiteral("人工录入原文"));
    QCOMPARE(vm->projectAggregate()->findPage(pageId)->findRegion(regionId)->source().text,
             std::string("人工录入原文"));
    QVERIFY(vm->state().saveState.isDirty);

    // Saving continues normally
    QSignalSpy saveSpy(vm, &EditorViewModel::saveCompleted);
    vm->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!vm->state().saveState.isDirty);

    // Exporting continues normally
    const QString exportOutDir = fixture.stack.projectRoot(QStringLiteral("ocr_missing_export_out"));
    QSignalSpy exportSpy(vm, &EditorViewModel::exportCompleted);
    vm->exportPages(ExportScope::AllPages, exportOutDir);
    QVERIFY(exportSpy.wait(kAwaitTimeoutMs));
    QVERIFY(QFileInfo::exists(QDir(exportOutDir).filePath(QStringLiteral("page_0001_translated.png"))));

    vm->closeProjectSession();
}

void RecoveryMatrixTest::credentialMissingOr401KeepsManualTranslationAndCandidates() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("credential_401_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const domain::RectGeometry geom{50.0, 50.0, 120.0, 60.0};
    vm->createRegion(geom);
    const auto regionId = vm->state().regions.first().id;

    // Set initial manual translation and candidates in domain
    vm->commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, QStringLiteral("こんにちは"));

    domain::TranslationEntry trans;
    domain::TranslationCandidate cand{
        .id = "cand-1",
        .text = "Hello Candidate",
        .confidence = 1.0,
        .provider = "openai-compatible",
    };
    trans.setModelResult("Previous Model Result", {cand});
    trans.setManualText("Hello Manual");

    domain::PageRegionEdit edit{.regionId = regionId, .translation = trans};
    QVERIFY(vm->projectAggregate()->applyPageRegionEdits(pageId, {&edit, 1}));

    auto* page = vm->projectAggregate()->findPage(pageId);
    auto* region = page->findRegion(regionId);
    const auto manualTextBefore = region->translation().manualText().value_or("");
    const auto candidatesCountBefore = region->translation().candidates().size();
    QCOMPARE(manualTextBefore, std::string("Hello Manual"));
    QCOMPARE(candidatesCountBefore, 1);

    // 1. Missing credential error
    application::test::FakeCredentialStore emptyCredentials;
    network::ProviderConfig configNoKey{
        .providerId = QStringLiteral("openai"),
        .endpoint = QUrl(QStringLiteral("http://127.0.0.1:8080/v1/chat/completions")),
        .model = QStringLiteral("gpt-4o-mini"),
    };
    network::OpenAiCompatibleTranslator translatorNoKey(configNoKey,
                                                        &emptyCredentials,
                                                        nullptr,
                                                        network::RetryPolicy(nullptr, nullptr),
                                                        network::EndpointPolicy::AllowLoopbackHttp);

    TranslationRequest transReq{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{regionId, QStringLiteral("こんにちは")}},
        .glossary = {},
    };

    auto noKeyFuture = translatorNoKey.translate(transReq, OperationContext{});
    QVERIFY(awaitFuture(noKeyFuture));
    auto noKeyRes = noKeyFuture.result();
    QVERIFY(!noKeyRes.has_value());
    const auto& noKeyError = noKeyRes.error();
    QCOMPARE(noKeyError.recovery, RecoveryAction::Reauthenticate);

    RecoveryPresenter presenter;
    const auto presNoKey = presenter.present(noKeyError);
    QVERIFY(presNoKey.has_value());
    QCOMPARE(presNoKey->action, RecoveryAction::Reauthenticate);
    QCOMPARE(presNoKey->actionText, QStringLiteral("配置 API Key"));
    QCOMPARE(RecoveryPresenter::settingsRouteForAction(presNoKey->action), QStringLiteral("settings/ai-translation"));

    // 2. Loopback 401 Unauthorized
    LoopbackHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(
        401,
        QByteArrayLiteral(
            "{\"error\":{\"message\":\"Incorrect API key provided\",\"type\":\"invalid_request_error\"}}"));

    application::test::FakeCredentialStore dummyCredentials;
    QVERIFY(
        dummyCredentials.writeApiKey(QStringLiteral("openai"), SecretBytes(QByteArrayLiteral("sk-dummy"))).has_value());

    network::ProviderConfig config401{
        .providerId = QStringLiteral("openai"),
        .endpoint = server.endpoint(),
        .model = QStringLiteral("gpt-4o-mini"),
    };
    network::OpenAiCompatibleTranslator translator401(config401,
                                                      &dummyCredentials,
                                                      nullptr,
                                                      network::RetryPolicy(nullptr, nullptr),
                                                      network::EndpointPolicy::AllowLoopbackHttp);

    auto auth401Future = translator401.translate(transReq, OperationContext{});
    QVERIFY(awaitFuture(auth401Future));
    auto auth401Res = auth401Future.result();
    QVERIFY(!auth401Res.has_value());
    const auto& error401 = auth401Res.error();
    QCOMPARE(error401.code, QStringLiteral("network.unauthorized"));
    QCOMPARE(error401.recovery, RecoveryAction::Reauthenticate);

    const auto pres401 = presenter.present(error401);
    QVERIFY(pres401.has_value());
    QCOMPARE(pres401->action, RecoveryAction::Reauthenticate);

    // Verify manual translation and candidates preserved
    auto* regionAfter = vm->projectAggregate()->findPage(pageId)->findRegion(regionId);
    QCOMPARE(regionAfter->translation().manualText().value_or(""), manualTextBefore);
    QCOMPARE(regionAfter->translation().candidates().size(), candidatesCountBefore);

    vm->closeProjectSession();
}

void RecoveryMatrixTest::rateLimitTimeoutOfflineKeepAdoptedResults() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("rate_limit_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const domain::RectGeometry geom{60.0, 60.0, 140.0, 70.0};
    vm->createRegion(geom);
    const auto regionId = vm->state().regions.first().id;

    // Set adopted translation and source text
    vm->commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, QStringLiteral("ソーステキスト"));

    domain::TranslationEntry trans;
    domain::TranslationCandidate cand{
        .id = "adopted-1",
        .text = "Adopted Machine Translation",
        .confidence = 1.0,
        .provider = "openai-compatible",
    };
    trans.setModelResult("Adopted Machine Translation", {cand});
    trans.setManualText("Adopted Machine Translation");

    domain::PageRegionEdit edit{.regionId = regionId, .translation = trans};
    QVERIFY(vm->projectAggregate()->applyPageRegionEdits(pageId, {&edit, 1}));

    auto* region = vm->projectAggregate()->findPage(pageId)->findRegion(regionId);
    const auto sourceTextBefore = region->source().text;
    const auto translationBefore = region->translation().manualText().value_or("");
    QCOMPARE(sourceTextBefore, std::string("ソーステキスト"));
    QCOMPARE(translationBefore, std::string("Adopted Machine Translation"));

    // 1. Loopback 429 Too Many Requests
    LoopbackHttpServer server;
    QVERIFY(server.listen());
    server.enqueueResponse(
        429,
        QByteArrayLiteral("{\"error\":{\"message\":\"Rate limit reached\",\"type\":\"rate_limit_error\"}}"),
        {{QByteArrayLiteral("Retry-After"), QByteArrayLiteral("5")}});

    application::test::FakeCredentialStore credentials;
    QVERIFY(credentials.writeApiKey(QStringLiteral("openai"), SecretBytes(QByteArrayLiteral("sk-test"))).has_value());

    network::ProviderConfig config429{
        .providerId = QStringLiteral("openai"),
        .endpoint = server.endpoint(),
        .model = QStringLiteral("gpt-4o-mini"),
    };
    network::OpenAiCompatibleTranslator translator429(config429,
                                                      &credentials,
                                                      nullptr,
                                                      network::RetryPolicy(nullptr, nullptr, 0),
                                                      network::EndpointPolicy::AllowLoopbackHttp);

    TranslationRequest transReq{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{regionId, QStringLiteral("ソーステキスト")}},
        .glossary = {},
    };

    auto future429 = translator429.translate(transReq, OperationContext{});
    QVERIFY(awaitFuture(future429));
    auto res429 = future429.result();
    QVERIFY(!res429.has_value());
    const auto& error429 = res429.error();
    QCOMPARE(error429.code, QStringLiteral("network.rate_limited"));
    QCOMPARE(error429.recovery, RecoveryAction::Retry);
    QVERIFY(error429.retryable);

    RecoveryPresenter presenter;
    const auto pres429 = presenter.present(error429);
    QVERIFY(pres429.has_value());
    QCOMPARE(pres429->action, RecoveryAction::Retry);
    QCOMPARE(pres429->actionText, QStringLiteral("重试"));

    // 2. Offline / Server closed (connection refused)
    server.close();
    auto futureOffline = translator429.translate(transReq, OperationContext{});
    QVERIFY(awaitFuture(futureOffline));
    auto resOffline = futureOffline.result();
    QVERIFY(!resOffline.has_value());
    const auto& errorOffline = resOffline.error();
    QCOMPARE(errorOffline.category, ErrorCategory::Network);
    QCOMPARE(errorOffline.recovery, RecoveryAction::Retry);

    const auto presOffline = presenter.present(errorOffline);
    QVERIFY(presOffline.has_value());
    QCOMPARE(presOffline->action, RecoveryAction::Retry);

    // Verify adopted results and manual edits preserved
    auto* regionAfter = vm->projectAggregate()->findPage(pageId)->findRegion(regionId);
    QCOMPARE(regionAfter->source().text, sourceTextBefore);
    QCOMPARE(regionAfter->translation().manualText().value_or(""), translationBefore);

    vm->closeProjectSession();
}

void RecoveryMatrixTest::clearLayoutFailureWritesNoPartialDerivedAsset() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("clear_layout_failure_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const domain::RectGeometry geom{50.0, 50.0, 100.0, 50.0};
    vm->createRegion(geom);
    const auto regionId = vm->state().regions.first().id;

    const auto derivedTreeBefore = sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived"));
    const auto* page = vm->projectAggregate()->findPage(pageId);
    const auto* region = page->findRegion(regionId);
    const auto clearAssetBefore = page->clearLayerAsset();
    const auto layoutBefore = region->layout();
    const auto isClearedBefore = region->isCleared();
    const auto revisionBefore = region->contentRevision();

    // 1. Illegal Clear Request (e.g. null image)
    ClearRegionUseCase clearUseCase(fixture.stack.inpainter.get(), &fixture.stack.operationRegistry);
    ClearRequest illegalClearReq{
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .regionId = regionId,
        .pageImage = QImage(), // null image!
        .region = geom,
        .settings = domain::ClearSettings{.enabled = true},
    };
    OperationDescriptor clearDesc{
        .stage = OperationStage::Clear,
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .regionId = regionId,
    };
    OperationId clearOpId;
    auto clearFuture = clearUseCase.execute(illegalClearReq, clearDesc, vm->projectSessionGeneration(), clearOpId);
    QVERIFY(awaitFuture(clearFuture));
    auto clearRes = clearFuture.result();
    QVERIFY(!clearRes.has_value());
    QCOMPARE(clearRes.error().category, ErrorCategory::Input);

    // 2. Illegal Layout Request (e.g. empty text)
    LayoutRegionUseCase layoutUseCase(fixture.stack.layouter.get(), &fixture.stack.operationRegistry);
    LayoutRequest illegalLayoutReq{
        .pageId = pageId,
        .regionId = regionId,
        .text = QString(), // empty text!
        .bounds = geom,
    };
    OperationDescriptor layoutDesc{
        .stage = OperationStage::Layout,
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .regionId = regionId,
    };
    OperationId layoutOpId;
    auto layoutFuture = layoutUseCase.execute(illegalLayoutReq, layoutDesc, vm->projectSessionGeneration(), layoutOpId);
    QVERIFY(awaitFuture(layoutFuture));
    auto layoutRes = layoutFuture.result();
    QVERIFY(!layoutRes.has_value());
    QCOMPARE(layoutRes.error().category, ErrorCategory::Input);

    // Assert: assets/derived tree hash unchanged and domain references unchanged
    const auto derivedTreeAfterFailure = sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived"));
    QCOMPARE(derivedTreeAfterFailure, derivedTreeBefore);

    const auto* pageAfterFail = vm->projectAggregate()->findPage(pageId);
    const auto* regionAfterFail = pageAfterFail->findRegion(regionId);
    QCOMPARE(pageAfterFail->clearLayerAsset(), clearAssetBefore);
    QCOMPARE(regionAfterFail->layout(), layoutBefore);
    QCOMPARE(regionAfterFail->isCleared(), isClearedBefore);
    QCOMPARE(regionAfterFail->contentRevision(), revisionBefore);

    // 3. Retry with valid inputs -> succeeds
    ClearRequest validClearReq{
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .regionId = regionId,
        .pageImage = vm->currentPageImage(),
        .region = geom,
        .settings = domain::ClearSettings{.enabled = true},
    };
    auto validClearFut = clearUseCase.execute(validClearReq, clearDesc, vm->projectSessionGeneration(), clearOpId);
    QVERIFY(awaitFuture(validClearFut));
    auto validClearRes = validClearFut.result();
    QVERIFY(validClearRes.has_value());

    PrepareClearResultUseCase prepClear(vm->projectAggregate(), fixture.stack.derivedAssetStore.get());
    PrepareClearResultRequest prepClearReq{
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .expectedRegionId = regionId,
        .pageSize = vm->currentPageImage().size(),
        .projectRoot = fixture.projectRoot,
        .result = validClearRes.value(),
        .sessionGeneration = vm->projectSessionGeneration(),
        .currentSessionGeneration = vm->projectSessionGeneration(),
        .expectedRevision = regionAfterFail->contentRevision(),
        .context = OperationContext{},
    };
    auto prepClearFut = prepClear.execute(prepClearReq);
    QVERIFY(awaitFuture(prepClearFut));
    auto prepClearRes = prepClearFut.result();
    QVERIFY(prepClearRes.has_value());

    ApplyPreparedClearChangeRequest applyReq{
        .projectId = vm->projectAggregate()->id(),
        .pageId = pageId,
        .expectedRegionId = regionId,
        .targetRef = prepClearRes.value().afterRef,
        .targetState = true,
        .sessionGeneration = vm->projectSessionGeneration(),
        .currentSessionGeneration = vm->projectSessionGeneration(),
        .expectedRevision = regionAfterFail->contentRevision(),
    };
    auto applyRes = ApplyPreparedClearChangeUseCase(vm->projectAggregate()).execute(applyReq);
    QVERIFY(applyRes.has_value());

    // Derived asset was cleanly written
    const auto derivedTreeFinal = sha256Tree(fixture.projectRoot, QStringLiteral("assets/derived"));
    QVERIFY(!derivedTreeFinal.isEmpty());
    QVERIFY(vm->projectAggregate()->findPage(pageId)->clearLayerAsset().has_value());

    vm->closeProjectSession();
}

void RecoveryMatrixTest::saveFailureKeepsDirtyAndLastValidFile() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("save_failure_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const QString projectJsonPath = fixture.projectRoot + QStringLiteral("/project.json");
    QFile jsonFile(projectJsonPath);
    QVERIFY(jsonFile.open(QIODevice::ReadOnly));
    const QByteArray jsonBytesBefore = jsonFile.readAll();
    jsonFile.close();
    QVERIFY(!jsonBytesBefore.isEmpty());

    // Make an edit to dirty project
    const domain::RectGeometry geom{30.0, 30.0, 110.0, 55.0};
    vm->createRegion(geom);
    QVERIFY(vm->state().saveState.isDirty);
    QVERIFY(!vm->undoStackIsCleanForTest());

    // Inject disk full error via disk space probe
    fixture.stack.repository->setDiskSpaceProbe([](const QString&) { return qint64(0); });

    QSignalSpy failedSpy(vm, &EditorViewModel::saveFailed);
    vm->saveProject();
    QVERIFY(failedSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(failedSpy.count(), 1);

    const auto error = failedSpy.first().at(0).value<ApplicationError>();
    QCOMPARE(error.code, QStringLiteral("persistence.disk_full"));
    QCOMPARE(error.category, ErrorCategory::Persistence);
    QCOMPARE(error.recovery, RecoveryAction::FreeDiskSpace);

    // Consume via RecoveryPresenter
    RecoveryPresenter presenter;
    const auto presentation = presenter.present(error);
    QVERIFY(presentation.has_value());
    QCOMPARE(presentation->action, RecoveryAction::FreeDiskSpace);
    QCOMPARE(presentation->actionText, QStringLiteral("查看位置"));
    QCOMPARE(presentation->safeMessage, QStringLiteral("磁盘空间不足，无法完成操作"));

    // Project remains dirty, undo clean point unchanged, project.json unmodified
    QVERIFY(vm->state().saveState.isDirty);
    QVERIFY(vm->projectAggregate()->isDirty());
    QVERIFY(!vm->undoStackIsCleanForTest());

    QFile jsonFileAfterFail(projectJsonPath);
    QVERIFY(jsonFileAfterFail.open(QIODevice::ReadOnly));
    const QByteArray jsonBytesAfterFail = jsonFileAfterFail.readAll();
    jsonFileAfterFail.close();
    QCOMPARE(jsonBytesAfterFail, jsonBytesBefore);

    // Restore probe and retry save -> succeeds
    fixture.stack.repository->setDiskSpaceProbe({});
    QSignalSpy saveSpy(vm, &EditorViewModel::saveCompleted);
    vm->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!vm->state().saveState.isDirty);
    QVERIFY(!vm->projectAggregate()->isDirty());
    QVERIFY(vm->undoStackIsCleanForTest());

    QFile jsonFileAfterSuccess(projectJsonPath);
    QVERIFY(jsonFileAfterSuccess.open(QIODevice::ReadOnly));
    const QByteArray jsonBytesAfterSuccess = jsonFileAfterSuccess.readAll();
    jsonFileAfterSuccess.close();
    QVERIFY(jsonBytesAfterSuccess != jsonBytesBefore);

    vm->closeProjectSession();
}

void RecoveryMatrixTest::exportFailureKeepsProjectAndExistingOutputs() {
    RecoveryFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("export_failure_recovery"), 1));

    auto* vm = fixture.viewModel.get();
    const auto pageId = fixture.pageIds[0];
    vm->selectPage(pageId);
    QTRY_VERIFY_WITH_TIMEOUT(!vm->state().isPageLoading, kAwaitTimeoutMs);

    const domain::RectGeometry geom{45.0, 45.0, 100.0, 50.0};
    vm->createRegion(geom);

    // Setup an export target directory with an existing file
    const QString exportDir = fixture.stack.projectRoot(QStringLiteral("read_only_export_dir"));
    QDir().mkpath(exportDir);
    const QString existingFilePath = QDir(exportDir).filePath(QStringLiteral("existing_output.txt"));
    QFile existingFile(existingFilePath);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    existingFile.write("Pre-existing exported artifact content");
    existingFile.close();

    const auto existingTreeBefore = sha256Tree(exportDir);
    QCOMPARE(existingTreeBefore.size(), 1);

    ChoosePathFailingExporter failingExporter;
    vm->setExportDependencies(&failingExporter, fixture.stack.derivedAssetStore.get());

    // Export to read-only directory
    QSignalSpy failSpy(vm, &EditorViewModel::exportFailed);
    vm->exportPages(ExportScope::AllPages, exportDir);
    QVERIFY(failSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(failSpy.count(), 1);

    const auto error = failSpy.first().at(0).value<ApplicationError>();
    QCOMPARE(error.category, ErrorCategory::Persistence);

    RecoveryPresenter presenter;
    const auto presentation = presenter.present(error);
    QVERIFY(presentation.has_value());
    // Error is mapped to ChoosePath recovery action
    QCOMPARE(presentation->action, RecoveryAction::ChoosePath);
    QCOMPARE(presentation->actionText, QStringLiteral("选择其他位置"));

    // Project aggregate and existing files in export directory remain intact
    const auto existingTreeAfter = sha256Tree(exportDir);
    QCOMPARE(existingTreeAfter, existingTreeBefore);

    // Retry with the real exporter at a valid destination.
    vm->setExportDependencies(fixture.stack.exporter.get(), fixture.stack.derivedAssetStore.get());

    const QString validExportDir = fixture.stack.projectRoot(QStringLiteral("valid_export_dir"));
    QSignalSpy successSpy(vm, &EditorViewModel::exportCompleted);
    vm->exportPages(ExportScope::AllPages, validExportDir);
    QVERIFY(successSpy.wait(kAwaitTimeoutMs));
    QVERIFY(QFileInfo::exists(QDir(validExportDir).filePath(QStringLiteral("page_0001_translated.png"))));

    vm->closeProjectSession();
}

void RecoveryMatrixTest::projectSwitchRejectsLateResults() {
    RealDataStack stack;

    // Create P1
    const QString p1Root = stack.projectRoot(QStringLiteral("project_p1_recovery"));
    CreateProjectRequest createP1Req{
        .name = QStringLiteral("Project 1"),
        .path = p1Root,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createP1Fut = CreateProjectUseCase(stack.repository.get()).execute(createP1Req, OperationContext{});
    QVERIFY(awaitFuture(createP1Fut) && createP1Fut.result().has_value());
    OpenProjectRequest openP1Req{.path = p1Root};
    auto openP1Fut = OpenProjectUseCase(stack.repository.get()).execute(openP1Req, OperationContext{});
    QVERIFY(awaitFuture(openP1Fut) && openP1Fut.result().has_value());
    auto p1 = openP1Fut.result().value().project;
    const QString img1 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_001.png"));
    ImportImagesRequest impP1Req{.projectId = p1.id(), .projectRoot = p1Root, .filePaths = {img1}};
    auto impP1Fut = ImportImagesUseCase(stack.importer.get()).execute(impP1Req, OperationContext{});
    QVERIFY(awaitFuture(impP1Fut) && impP1Fut.result().has_value());
    ApplyImportResultRequest appP1Req{.projectId = p1.id(), .result = impP1Fut.result().value()};
    auto appP1Res = ApplyImportResultUseCase(&p1).execute(appP1Req);
    QVERIFY(appP1Res.has_value());
    AcknowledgeImportedAssetsRequest ackP1Req{
        .projectId = p1.id(), .projectRoot = p1Root, .batchId = impP1Fut.result().value().batchId};
    auto ackP1Fut = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackP1Req, OperationContext{});
    QVERIFY(awaitFuture(ackP1Fut) && ackP1Fut.result().has_value());
    SaveProjectRequest saveP1Req{.project = p1};
    auto saveP1Fut = SaveProjectUseCase(stack.repository.get()).execute(saveP1Req, OperationContext{});
    QVERIFY(awaitFuture(saveP1Fut) && saveP1Fut.result().has_value());
    QVERIFY(p1.markCleanIfRevision(saveP1Fut.result().value().savedRevision));
    // Create P2
    const QString p2Root = stack.projectRoot(QStringLiteral("project_p2_recovery"));
    CreateProjectRequest createP2Req{
        .name = QStringLiteral("Project 2"),
        .path = p2Root,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createP2Fut = CreateProjectUseCase(stack.repository.get()).execute(createP2Req, OperationContext{});
    QVERIFY(awaitFuture(createP2Fut) && createP2Fut.result().has_value());
    OpenProjectRequest openP2Req{.path = p2Root};
    auto openP2Fut = OpenProjectUseCase(stack.repository.get()).execute(openP2Req, OperationContext{});
    QVERIFY(awaitFuture(openP2Fut) && openP2Fut.result().has_value());
    auto p2 = openP2Fut.result().value().project;
    const QString img2 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_002.png"));
    ImportImagesRequest impP2Req{.projectId = p2.id(), .projectRoot = p2Root, .filePaths = {img2}};
    auto impP2Fut = ImportImagesUseCase(stack.importer.get()).execute(impP2Req, OperationContext{});
    QVERIFY(awaitFuture(impP2Fut) && impP2Fut.result().has_value());
    ApplyImportResultRequest appP2Req{.projectId = p2.id(), .result = impP2Fut.result().value()};
    auto appP2Res = ApplyImportResultUseCase(&p2).execute(appP2Req);
    QVERIFY(appP2Res.has_value());
    AcknowledgeImportedAssetsRequest ackP2Req{
        .projectId = p2.id(), .projectRoot = p2Root, .batchId = impP2Fut.result().value().batchId};
    auto ackP2Fut = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackP2Req, OperationContext{});
    QVERIFY(awaitFuture(ackP2Fut) && ackP2Fut.result().has_value());
    SaveProjectRequest saveP2Req{.project = p2};
    auto saveP2Fut = SaveProjectUseCase(stack.repository.get()).execute(saveP2Req, OperationContext{});
    QVERIFY(awaitFuture(saveP2Fut) && saveP2Fut.result().has_value());
    QVERIFY(p2.markCleanIfRevision(saveP2Fut.result().value().savedRevision));
    CloseProjectRequest closeP2Req{.projectId = p2.id()};
    auto closeP2Fut = CloseProjectUseCase(stack.repository.get()).execute(closeP2Req, OperationContext{});
    QVERIFY(awaitFuture(closeP2Fut) && closeP2Fut.result().has_value());

    auto ocrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();
    auto viewModel = std::make_unique<EditorViewModel>(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    auto controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                   ocrEngine.get(),
                                                                   nullptr,
                                                                   stack.inpainter.get(),
                                                                   nullptr,
                                                                   stack.layouter.get(),
                                                                   stack.derivedAssetStore.get(),
                                                                   &stack.operationRegistry);

    // 1. Open P1
    OpenedProjectSession sessionP1{
        .project = p1,
        .projectRootPath = p1Root,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel->openProjectSession(std::move(sessionP1));
    QVERIFY(viewModel->hasSession());
    const auto p1PageId = viewModel->state().pages.first().id;
    viewModel->selectPage(p1PageId);

    const domain::RectGeometry geom1{50.0, 50.0, 100.0, 50.0};
    viewModel->createRegion(geom1);
    const domain::RegionId p1RegionId = viewModel->state().regions.first().id;

    // Start deferred OCR on P1
    ocrEngine->deferred = true;
    controller->recognizeRegion(p1PageId, p1RegionId);
    QCOMPARE(ocrEngine->callCount, 1);
    const quint64 p1SessionGen = viewModel->projectSessionGeneration();

    // 2. Close P1 and Open P2
    QSignalSpy projectClosedSpy(viewModel.get(), &EditorViewModel::projectClosed);
    viewModel->closeProjectSession();

    OpenedProjectSession sessionP2{
        .project = p2,
        .projectRootPath = p2Root,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel->openProjectSession(std::move(sessionP2));
    QVERIFY(projectClosedSpy.wait(kAwaitTimeoutMs));
    QVERIFY(viewModel->hasSession());
    QCOMPARE(viewModel->projectAggregate()->id(), p2.id());
    const auto p2PageId = viewModel->state().pages.first().id;
    viewModel->selectPage(p2PageId);

    const domain::RectGeometry geom2{80.0, 80.0, 120.0, 60.0};
    viewModel->createRegion(geom2);
    const domain::RegionId p2RegionId = viewModel->state().regions.first().id;
    const quint64 p2SessionGen = viewModel->projectSessionGeneration();
    QVERIFY(p2SessionGen > p1SessionGen);

    const int p2UndoCountBefore = viewModel->undoStackCountForTest();
    QCOMPARE(p2UndoCountBefore, 1);
    const auto* p2RegionBefore = viewModel->projectAggregate()->findPage(p2PageId)->findRegion(p2RegionId);
    const std::string p2TextBefore = p2RegionBefore->source().text;

    // 3. Complete P1's deferred OCR task
    ocrEngine->nextResponse.text = QStringLiteral("Stale P1 OCR Completion");
    QSignalSpy propSpy(controller.get(), &EditorProcessingController::proposalReady);
    QSignalSpy finishSpy(controller.get(), &EditorProcessingController::processingFinished);
    ocrEngine->complete();
    QCoreApplication::processEvents();

    // 4. Verify P1's completion was rejected with stale generation and silently discarded
    QCOMPARE(propSpy.count(), 0);
    QCOMPARE(finishSpy.count(), 0);
    QVERIFY(!controller->hasProposal(p1PageId));
    QVERIFY(!controller->hasProposal(p2PageId));

    // P2 domain aggregate and undo stack are untouched
    const auto* p2RegionAfter = viewModel->projectAggregate()->findPage(p2PageId)->findRegion(p2RegionId);
    QCOMPARE(p2RegionAfter->source().text, p2TextBefore);
    QCOMPARE(viewModel->undoStackCountForTest(), p2UndoCountBefore);

    viewModel->closeProjectSession();
}

QTEST_MAIN(RecoveryMatrixTest)
#include "recovery_matrix_test.moc"
