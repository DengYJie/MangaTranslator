#include "FakeCredentialStore.h"
#include "IntegrationHarness.h"
#include "LoopbackHttpServer.h"
#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/dto/TranslationDto.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/TextContent.h"
#include "domain/model/TranslationEntry.h"
#include "network/translation/ChatCompletionsProtocol.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"
#include "network/translation/QtHttpTransport.h"
#include "network/translation/RetryPolicy.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace manga;
using namespace manga::test;
using namespace manga::application;

namespace {

QByteArray makeChatCompletionsResponse(const QVector<TranslationResponseItem>& items) {
    QJsonArray jsonItems;
    for (const auto& item : items) {
        QJsonArray candArr;
        for (const auto& c : item.candidates) {
            candArr.append(c);
        }
        QJsonObject itemObj{
            {QStringLiteral("regionId"), QString::fromStdString(item.regionId.toString())},
            {QStringLiteral("sourceText"), item.sourceText},
            {QStringLiteral("translation"), item.translation},
            {QStringLiteral("candidates"), candArr},
        };
        jsonItems.append(itemObj);
    }
    QJsonObject contentObj{{QStringLiteral("items"), jsonItems}};
    const QString contentStr = QString::fromUtf8(QJsonDocument(contentObj).toJson(QJsonDocument::Compact));
    QJsonObject choiceObj{{QStringLiteral("finish_reason"), QStringLiteral("stop")},
                          {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), contentStr}}}};
    QJsonObject rootObj{{QStringLiteral("choices"), QJsonArray{choiceObj}}};
    return QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
}

struct TranslationScenarioFixture {
    RealDataStack stack;
    LoopbackHttpServer server;
    application::test::FakeCredentialStore credentialStore;
    QString projectRoot;
    domain::ComicProject project{"TranslationScenarioTestProject"};
    domain::PageId pageId;
    QVector<domain::RegionId> regionIds;
    network::ProviderConfig providerConfig;
    std::unique_ptr<network::OpenAiCompatibleTranslator> translator;

    bool init(const QString& scenarioName,
              const QStringList& initialSourceTexts = {},
              network::RetryPolicy retryPolicy = network::RetryPolicy(nullptr, nullptr)) {
        if (!server.listen()) {
            return false;
        }

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
        project = openFuture.result().value().project;

        const QString fixturesDir = QStringLiteral(MANGA_TEST_FIXTURES_DIR);
        const QString page1Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_001.png"));
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

        if (project.pages().empty()) {
            return false;
        }
        pageId = project.pages().front().id();

        for (int i = 0; i < initialSourceTexts.size(); ++i) {
            domain::RegionId rId = domain::RegionId::generate();
            domain::RectGeometry geom{50.0 + i * 120.0, 50.0, 100.0, 80.0};
            domain::TextRegion region(rId, geom, i);
            auto addRes = project.addRegionToPage(pageId, region);
            if (!addRes.has_value()) {
                return false;
            }
            domain::TextContent sourceContent;
            sourceContent.setManualText(initialSourceTexts[i].toStdString());
            domain::PageRegionEdit edit{.regionId = rId, .source = sourceContent};
            auto editRes = project.applyPageRegionEdits(pageId, {&edit, 1});
            if (!editRes.has_value()) {
                return false;
            }
            regionIds.append(rId);
        }

        if (!credentialStore.writeApiKey(QStringLiteral("openai-compatible"), SecretBytes("sk-fake-secret-key-12345"))
                 .has_value()) {
            return false;
        }

        providerConfig = network::ProviderConfig{
            .providerId = QStringLiteral("openai-compatible"),
            .endpoint = server.endpoint(),
            .model = QStringLiteral("gpt-4o-mini"),
            .timeoutMs = 15000,
            .structuredOutputMode = network::StructuredOutputMode::JsonSchemaStrict,
            .maxCompletionTokens = 2048,
            .temperature = 0.2,
        };

        translator = std::make_unique<network::OpenAiCompatibleTranslator>(providerConfig,
                                                                           &credentialStore,
                                                                           std::make_unique<network::QtHttpTransport>(),
                                                                           std::move(retryPolicy),
                                                                           network::EndpointPolicy::AllowLoopbackHttp);

        return true;
    }
};

} // namespace

class TranslationScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void loopbackChatCompletionsRoundTrip();
    void regionIdAlignmentRejectsUnknownOrDuplicatedIds();
    void credential401MapsToReauthenticateAndKeepsManualText();
    void retryable429HonorsRetryAfterWithFixedClock();
    void adoptedTranslationSurvivesSaveReopen();
};

void TranslationScenarioTest::initTestCase() {
    qRegisterMetaType<domain::PageId>();
    qRegisterMetaType<domain::RegionId>();
    qRegisterMetaType<application::OperationId>();
}

void TranslationScenarioTest::loopbackChatCompletionsRoundTrip() {
    TranslationScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("round_trip"), {QStringLiteral("こんにちは"), QStringLiteral("世界")}));

    const auto r1 = fixture.regionIds[0];
    const auto r2 = fixture.regionIds[1];

    QVector<TranslationResponseItem> responseItems{
        {
            .regionId = r1,
            .sourceText = QStringLiteral("こんにちは"),
            .translation = QStringLiteral("Hello"),
            .candidates = {QStringLiteral("Hello"), QStringLiteral("Hi")},
        },
        {
            .regionId = r2,
            .sourceText = QStringLiteral("世界"),
            .translation = QStringLiteral("World"),
            .candidates = {QStringLiteral("World"), QStringLiteral("The world")},
        },
    };
    fixture.server.enqueueResponse(200, makeChatCompletionsResponse(responseItems));

    TranslatePageUseCase useCase(fixture.translator.get(), &fixture.stack.operationRegistry);
    TranslationRequest req{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{r1, QStringLiteral("こんにちは")}, {r2, QStringLiteral("世界")}},
        .glossary = {},
    };
    OperationDescriptor desc{OperationStage::Translate,
                             fixture.project.id(),
                             fixture.pageId,
                             std::nullopt,
                             QStringLiteral("Translate"),
                             true};
    OperationId opId;
    auto future = useCase.execute(req, desc, 1, opId);
    QVERIFY(awaitFuture(future));
    auto result = future.result();
    QVERIFY(result.has_value());

    // Verify response items and providerId
    QCOMPARE(result->providerId, QStringLiteral("openai-compatible"));
    QCOMPARE(result->items.size(), 2);
    QCOMPARE(result->items[0].regionId, r1);
    QCOMPARE(result->items[0].sourceText, QStringLiteral("こんにちは"));
    QCOMPARE(result->items[0].translation, QStringLiteral("Hello"));
    QCOMPARE(result->items[0].candidates, QVector<QString>({QStringLiteral("Hello"), QStringLiteral("Hi")}));
    QCOMPARE(result->items[1].regionId, r2);
    QCOMPARE(result->items[1].sourceText, QStringLiteral("世界"));
    QCOMPARE(result->items[1].translation, QStringLiteral("World"));
    QCOMPARE(result->items[1].candidates, QVector<QString>({QStringLiteral("World"), QStringLiteral("The world")}));

    // Verify recorded HTTP request on loopback server
    QCOMPARE(fixture.server.requestCount(), 1);
    const auto lastReq = fixture.server.lastRequest();
    QCOMPARE(lastReq.authorizationHeader(), QByteArrayLiteral("Bearer sk-fake-secret-key-12345"));

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(lastReq.body, &parseErr);
    QCOMPARE(parseErr.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();

    QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(root.value(QStringLiteral("temperature")).toDouble(), 0.2);
    QCOMPARE(root.value(QStringLiteral("max_completion_tokens")).toInt(), 2048);

    // Messages assertion
    const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QVERIFY(!messages[0].toObject().value(QStringLiteral("content")).toString().isEmpty());

    QCOMPARE(messages[1].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const QString userJsonStr = messages[1].toObject().value(QStringLiteral("content")).toString();
    const QJsonDocument userDoc = QJsonDocument::fromJson(userJsonStr.toUtf8());
    QVERIFY(userDoc.isObject());
    const QJsonObject userPayload = userDoc.object();
    QCOMPARE(userPayload.value(QStringLiteral("sourceLanguage")).toString(), QStringLiteral("ja"));
    QCOMPARE(userPayload.value(QStringLiteral("targetLanguage")).toString(), QStringLiteral("en"));
    QCOMPARE(userPayload.value(QStringLiteral("readingDirection")).toString(), QStringLiteral("right-to-left"));
    const QJsonArray userRegions = userPayload.value(QStringLiteral("regions")).toArray();
    QCOMPARE(userRegions.size(), 2);
    QCOMPARE(userRegions[0].toObject().value(QStringLiteral("regionId")).toString(),
             QString::fromStdString(r1.toString()));
    QCOMPARE(userRegions[0].toObject().value(QStringLiteral("sourceText")).toString(), QStringLiteral("こんにちは"));
    QCOMPARE(userRegions[1].toObject().value(QStringLiteral("regionId")).toString(),
             QString::fromStdString(r2.toString()));
    QCOMPARE(userRegions[1].toObject().value(QStringLiteral("sourceText")).toString(), QStringLiteral("世界"));

    // Response format assertion
    const QJsonObject respFormat = root.value(QStringLiteral("response_format")).toObject();
    QCOMPARE(respFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    const QJsonObject jsonSchema = respFormat.value(QStringLiteral("json_schema")).toObject();
    QCOMPARE(jsonSchema.value(QStringLiteral("name")).toString(), QStringLiteral("manga_translation_response_v1"));
    QCOMPARE(jsonSchema.value(QStringLiteral("strict")).toBool(), true);
    QVERIFY(jsonSchema.value(QStringLiteral("schema")).isObject());

    // Candidate alignment via PrepareProcessingAdoptionUseCase
    PrepareProcessingAdoptionRequest prepReq{
        .projectId = fixture.project.id(),
        .pageId = fixture.pageId,
        .stage = OperationStage::Translate,
        .sessionGeneration = 1,
        .currentSessionGeneration = 1,
        .translation = result.value(),
        .expectedRevisions =
            {
                {r1, fixture.project.findPage(fixture.pageId)->findRegion(r1)->contentRevision()},
                {r2, fixture.project.findPage(fixture.pageId)->findRegion(r2)->contentRevision()},
            },
        .projectRoot = fixture.projectRoot,
        .replaceManualPolicy = ReplaceManualPolicy::Keep,
    };
    PrepareClearResultUseCase clearUseCase(&fixture.project, fixture.stack.derivedAssetStore.get());
    PrepareProcessingAdoptionUseCase prepUseCase(&fixture.project, &clearUseCase);
    auto prepFuture = prepUseCase.execute(prepReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(prepRes.has_value());
    QCOMPARE(prepRes->changes.size(), 2);
    QVERIFY(prepRes->skipped.isEmpty());

    const auto& change1 = (prepRes->changes[0].regionId == r1) ? prepRes->changes[0] : prepRes->changes[1];
    const auto& change2 = (prepRes->changes[0].regionId == r2) ? prepRes->changes[0] : prepRes->changes[1];

    QVERIFY(change1.after.translation.has_value());
    QCOMPARE(change1.after.translation->modelText().value_or(""), std::string("Hello"));
    QCOMPARE(change1.after.translation->candidates().size(), 2);
    QCOMPARE(change1.after.translation->candidates()[0].text, std::string("Hello"));
    QCOMPARE(change1.after.translation->candidates()[0].provider, std::string("openai-compatible"));
    QCOMPARE(change1.after.translation->candidates()[1].text, std::string("Hi"));

    QVERIFY(change2.after.translation.has_value());
    QCOMPARE(change2.after.translation->modelText().value_or(""), std::string("World"));
    QCOMPARE(change2.after.translation->candidates().size(), 2);
    QCOMPARE(change2.after.translation->candidates()[0].text, std::string("World"));
    QCOMPARE(change2.after.translation->candidates()[1].text, std::string("The world"));
}

void TranslationScenarioTest::regionIdAlignmentRejectsUnknownOrDuplicatedIds() {
    TranslationScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("unknown_dup_ids"), {QStringLiteral("有効な原文")}));

    const auto validRegionId = fixture.regionIds[0];
    const auto unknownRegionId = domain::RegionId::generate();

    // 1. Server returns candidates containing both a valid RegionId and an unknown RegionId
    QVector<TranslationResponseItem> responseItems{
        {
            .regionId = validRegionId,
            .sourceText = QStringLiteral("有効な原文"),
            .translation = QStringLiteral("Valid Translation"),
            .candidates = {QStringLiteral("Valid Translation")},
        },
        {
            .regionId = unknownRegionId,
            .sourceText = QStringLiteral("未知の原文"),
            .translation = QStringLiteral("Unknown Translation"),
            .candidates = {QStringLiteral("Unknown Translation")},
        },
    };
    fixture.server.enqueueResponse(200, makeChatCompletionsResponse(responseItems));

    TranslatePageUseCase useCase(fixture.translator.get(), &fixture.stack.operationRegistry);
    TranslationRequest req{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{validRegionId, QStringLiteral("有効な原文")}},
        .glossary = {},
    };
    OperationDescriptor desc{OperationStage::Translate,
                             fixture.project.id(),
                             fixture.pageId,
                             std::nullopt,
                             QStringLiteral("Translate"),
                             true};
    OperationId opId;
    auto future = useCase.execute(req, desc, 1, opId);
    QVERIFY(awaitFuture(future));
    auto result = future.result();
    QVERIFY(result.has_value());
    QCOMPARE(result->items.size(), 2);

    // Prepare adoption: unknown RegionId is rejected, valid RegionId is retained
    PrepareProcessingAdoptionRequest prepReq{
        .projectId = fixture.project.id(),
        .pageId = fixture.pageId,
        .stage = OperationStage::Translate,
        .sessionGeneration = 1,
        .currentSessionGeneration = 1,
        .translation = result.value(),
        .expectedRevisions =
            {
                {validRegionId, fixture.project.findPage(fixture.pageId)->findRegion(validRegionId)->contentRevision()},
            },
        .projectRoot = fixture.projectRoot,
        .replaceManualPolicy = ReplaceManualPolicy::Keep,
    };
    PrepareClearResultUseCase clearUseCase(&fixture.project, fixture.stack.derivedAssetStore.get());
    PrepareProcessingAdoptionUseCase prepUseCase(&fixture.project, &clearUseCase);
    auto prepFuture = prepUseCase.execute(prepReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(prepRes.has_value());

    // Only valid region change is prepared
    QCOMPARE(prepRes->changes.size(), 1);
    QCOMPARE(prepRes->changes[0].regionId, validRegionId);
    QCOMPARE(prepRes->changes[0].after.translation->modelText().value_or(""), std::string("Valid Translation"));

    // Unknown region is skipped with project.region_not_found
    QCOMPARE(prepRes->skipped.size(), 1);
    QCOMPARE(prepRes->skipped[0].regionId, std::optional<domain::RegionId>(unknownRegionId));
    QCOMPARE(prepRes->skipped[0].stage, QStringLiteral("translate"));
    QCOMPARE(prepRes->skipped[0].error.code, QStringLiteral("project.region_not_found"));

    // 2. Response containing duplicate RegionIds: both duplicate items rejected
    TranslationResponse duplicateResponse{
        .providerId = QStringLiteral("openai-compatible"),
        .items =
            {
                {
                    .regionId = validRegionId,
                    .sourceText = QStringLiteral("有効な原文"),
                    .translation = QStringLiteral("Duplicate A"),
                    .candidates = {QStringLiteral("Duplicate A")},
                },
                {
                    .regionId = validRegionId,
                    .sourceText = QStringLiteral("有効な原文"),
                    .translation = QStringLiteral("Duplicate B"),
                    .candidates = {QStringLiteral("Duplicate B")},
                },
            },
    };
    PrepareProcessingAdoptionRequest dupPrepReq{
        .projectId = fixture.project.id(),
        .pageId = fixture.pageId,
        .stage = OperationStage::Translate,
        .sessionGeneration = 1,
        .currentSessionGeneration = 1,
        .translation = duplicateResponse,
        .expectedRevisions =
            {
                {validRegionId, fixture.project.findPage(fixture.pageId)->findRegion(validRegionId)->contentRevision()},
            },
        .projectRoot = fixture.projectRoot,
        .replaceManualPolicy = ReplaceManualPolicy::Keep,
    };
    auto dupPrepFuture = prepUseCase.execute(dupPrepReq);
    QVERIFY(awaitFuture(dupPrepFuture));
    auto dupPrepRes = dupPrepFuture.result();
    QVERIFY(dupPrepRes.has_value());

    // Changes should be empty because duplicate RegionIds are rejected
    QCOMPARE(dupPrepRes->changes.size(), 0);
    QCOMPARE(dupPrepRes->skipped.size(), 2);
    QCOMPARE(dupPrepRes->skipped[0].error.code, QStringLiteral("protocol.duplicate_id"));
    QCOMPARE(dupPrepRes->skipped[1].error.code, QStringLiteral("protocol.duplicate_id"));
}

void TranslationScenarioTest::credential401MapsToReauthenticateAndKeepsManualText() {
    TranslationScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("cred_401"), {QStringLiteral("原文テキスト")}));

    const auto r1 = fixture.regionIds[0];

    // Set initial manual translation and an existing candidate
    domain::TranslationEntry initialEntry;
    domain::TranslationCandidate oldCand{
        .id = "cand-prev-1",
        .text = "Previous Candidate",
        .confidence = 1.0,
        .provider = "openai-compatible",
    };
    initialEntry.setModelResult("Previous Model Result", {oldCand});
    initialEntry.setManualText("My Manual Override");

    domain::PageRegionEdit edit{.regionId = r1, .translation = initialEntry};
    QVERIFY(fixture.project.applyPageRegionEdits(fixture.pageId, {&edit, 1}));

    const auto* originalRegion = fixture.project.findPage(fixture.pageId)->findRegion(r1);
    QVERIFY(originalRegion != nullptr);
    const auto initialRevision = originalRegion->contentRevision();
    const auto initialTranslation = originalRegion->translation();
    const auto initialSource = originalRegion->source();

    // Loopback server returns 401 Unauthorized
    fixture.server.enqueueResponse(
        401,
        QByteArrayLiteral(
            "{\"error\":{\"message\":\"Incorrect API key provided\",\"type\":\"invalid_request_error\"}}"));

    TranslatePageUseCase useCase(fixture.translator.get(), &fixture.stack.operationRegistry);
    TranslationRequest req{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{r1, QStringLiteral("原文テキスト")}},
        .glossary = {},
    };
    OperationDescriptor desc{OperationStage::Translate,
                             fixture.project.id(),
                             fixture.pageId,
                             std::nullopt,
                             QStringLiteral("Translate"),
                             true};
    OperationId opId;
    auto future = useCase.execute(req, desc, 1, opId);
    QVERIFY(awaitFuture(future));
    auto result = future.result();

    // Assert 401 maps to Network error category and Reauthenticate recovery action
    QVERIFY(!result.has_value());
    const auto& err = result.error();
    QCOMPARE(err.category, ErrorCategory::Network);
    QCOMPARE(err.code, QStringLiteral("network.unauthorized"));
    QCOMPARE(err.recovery, RecoveryAction::Reauthenticate);

    // Verify manual text, existing candidates and revision remain strictly untouched
    const auto* afterRegion = fixture.project.findPage(fixture.pageId)->findRegion(r1);
    QVERIFY(afterRegion != nullptr);
    QCOMPARE(afterRegion->contentRevision(), initialRevision);
    QCOMPARE(afterRegion->source(), initialSource);
    QCOMPARE(afterRegion->translation(), initialTranslation);
    QCOMPARE(afterRegion->translation().manualText().value_or(""), std::string("My Manual Override"));
    QCOMPARE(afterRegion->translation().effectiveText(), std::string("My Manual Override"));
    QCOMPARE(afterRegion->translation().candidates().size(), 1);
    QCOMPARE(afterRegion->translation().candidates()[0].text, std::string("Previous Candidate"));
}

void TranslationScenarioTest::retryable429HonorsRetryAfterWithFixedClock() {
    // 1. Direct RetryPolicy decision validation with fixed clock and RNG (no wall-clock wait)
    const QDateTime fixedNow = QDateTime::fromString(QStringLiteral("2026-09-01T12:00:00Z"), Qt::ISODate);
    auto fixedClock = [fixedNow]() { return fixedNow; };
    auto fixedRng = []() { return 0.5; };
    network::RetryPolicy policy(fixedClock, fixedRng, 2);

    const auto rateLimitFailure = network::classifyHttpStatus(429);
    QVERIFY(rateLimitFailure.retryable);
    QCOMPARE(rateLimitFailure.error.category, ErrorCategory::Network);
    QCOMPARE(rateLimitFailure.error.code, QStringLiteral("network.rate_limited"));

    // Integer seconds header
    const auto decisionInteger = policy.decide(rateLimitFailure, 0, "5", 30000);
    QVERIFY(decisionInteger.shouldRetry);
    QCOMPARE(decisionInteger.delayMs, 5000);

    // HTTP-date format header matching delta
    const auto decisionHttpDate = policy.decide(rateLimitFailure, 0, "Tue, 01 Sep 2026 12:00:10 GMT", 30000);
    QVERIFY(decisionHttpDate.shouldRetry);
    QCOMPARE(decisionHttpDate.delayMs, 10000);

    // Exceeding deadline should not retry
    const auto decisionExceeds = policy.decide(rateLimitFailure, 0, "45", 30000);
    QVERIFY(!decisionExceeds.shouldRetry);

    // Max retries exceeded should not retry
    const auto decisionMaxRetries = policy.decide(rateLimitFailure, 2, "0", 30000);
    QVERIFY(!decisionMaxRetries.shouldRetry);

    // Non-retryable error (e.g. 401) should not retry
    const auto authFailure = network::classifyHttpStatus(401);
    QVERIFY(!authFailure.retryable);
    const auto decisionAuth = policy.decide(authFailure, 0, "0", 30000);
    QVERIFY(!decisionAuth.shouldRetry);

    // 2. End-to-end integration test with real QtHttpTransport + loopback server:
    // 1st attempt: 429 + Retry-After: 0 (or delta 0) -> policy retries boundedly
    // 2nd attempt: 200 OK -> succeeds without wall-clock sleep
    TranslationScenarioFixture fixture;
    network::RetryPolicy translatorPolicy(fixedClock, []() { return 0.0; }, 2);
    QVERIFY(fixture.init(QStringLiteral("retry_429"), {QStringLiteral("リトライ原文")}, translatorPolicy));

    const auto r1 = fixture.regionIds[0];

    // Enqueue 429 on first attempt with Retry-After: 0
    fixture.server.enqueueResponse(429,
                                   QByteArrayLiteral("{\"error\":{\"message\":\"Rate limit reached\"}}"),
                                   {{QByteArrayLiteral("Retry-After"), QByteArrayLiteral("0")}});

    // Enqueue 200 on second attempt
    QVector<TranslationResponseItem> responseItems{
        {
            .regionId = r1,
            .sourceText = QStringLiteral("リトライ原文"),
            .translation = QStringLiteral("Retry Succeeded"),
            .candidates = {QStringLiteral("Retry Succeeded")},
        },
    };
    fixture.server.enqueueResponse(200, makeChatCompletionsResponse(responseItems));

    TranslatePageUseCase useCase(fixture.translator.get(), &fixture.stack.operationRegistry);
    TranslationRequest req{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{r1, QStringLiteral("リトライ原文")}},
        .glossary = {},
    };
    OperationDescriptor desc{OperationStage::Translate,
                             fixture.project.id(),
                             fixture.pageId,
                             std::nullopt,
                             QStringLiteral("Translate"),
                             true};
    OperationId opId;
    auto future = useCase.execute(req, desc, 1, opId);
    QVERIFY(awaitFuture(future));
    auto result = future.result();

    QVERIFY(result.has_value());
    QCOMPARE(result->items.size(), 1);
    QCOMPARE(result->items[0].translation, QStringLiteral("Retry Succeeded"));

    // Verify 2 requests were recorded on loopback server (initial attempt + retry)
    QCOMPARE(fixture.server.requestCount(), 2);
}

void TranslationScenarioTest::adoptedTranslationSurvivesSaveReopen() {
    TranslationScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("save_reopen"), {QStringLiteral("保存再開テスト")}));

    const auto r1 = fixture.regionIds[0];

    QVector<TranslationResponseItem> responseItems{
        {
            .regionId = r1,
            .sourceText = QStringLiteral("保存再開テスト"),
            .translation = QStringLiteral("Save Reopen Model Result"),
            .candidates = {QStringLiteral("Save Reopen Model Result"), QStringLiteral("Alternative Line")},
        },
    };
    fixture.server.enqueueResponse(200, makeChatCompletionsResponse(responseItems));

    TranslatePageUseCase useCase(fixture.translator.get(), &fixture.stack.operationRegistry);
    TranslationRequest req{
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
        .regions = {{r1, QStringLiteral("保存再開テスト")}},
        .glossary = {},
    };
    OperationDescriptor desc{OperationStage::Translate,
                             fixture.project.id(),
                             fixture.pageId,
                             std::nullopt,
                             QStringLiteral("Translate"),
                             true};
    OperationId opId;
    auto future = useCase.execute(req, desc, 1, opId);
    QVERIFY(awaitFuture(future));
    auto result = future.result();
    QVERIFY(result.has_value());

    // Prepare and adopt translation
    PrepareProcessingAdoptionRequest prepReq{
        .projectId = fixture.project.id(),
        .pageId = fixture.pageId,
        .stage = OperationStage::Translate,
        .sessionGeneration = 1,
        .currentSessionGeneration = 1,
        .translation = result.value(),
        .expectedRevisions =
            {
                {r1, fixture.project.findPage(fixture.pageId)->findRegion(r1)->contentRevision()},
            },
        .projectRoot = fixture.projectRoot,
        .replaceManualPolicy = ReplaceManualPolicy::Keep,
    };
    PrepareClearResultUseCase clearUseCase(&fixture.project, fixture.stack.derivedAssetStore.get());
    PrepareProcessingAdoptionUseCase prepUseCase(&fixture.project, &clearUseCase);
    auto prepFuture = prepUseCase.execute(prepReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(prepRes.has_value());
    QCOMPARE(prepRes->changes.size(), 1);

    // Apply prepared changes into domain aggregate
    const auto& change = prepRes->changes.first();
    domain::PageRegionEdit edit{.regionId = r1, .translation = change.after.translation};
    QVERIFY(fixture.project.applyPageRegionEdits(fixture.pageId, {&edit, 1}));

    // Add manual text as well
    auto currentEntry = fixture.project.findPage(fixture.pageId)->findRegion(r1)->translation();
    currentEntry.setManualText("Polished Manual Translation");
    domain::PageRegionEdit manualEdit{.regionId = r1, .translation = currentEntry};
    QVERIFY(fixture.project.applyPageRegionEdits(fixture.pageId, {&manualEdit, 1}));

    const auto originalTranslation = fixture.project.findPage(fixture.pageId)->findRegion(r1)->translation();
    const auto originalSource = fixture.project.findPage(fixture.pageId)->findRegion(r1)->source();
    QVERIFY(originalTranslation.hasManualText());
    QCOMPARE(originalTranslation.candidates().size(), 2);

    // Save project
    SaveProjectRequest saveReq{.project = fixture.project};
    auto saveFuture = SaveProjectUseCase(fixture.stack.repository.get()).execute(saveReq, OperationContext{});
    QVERIFY(awaitFuture(saveFuture));
    QVERIFY(saveFuture.result().has_value());
    QVERIFY(fixture.project.markCleanIfRevision(saveFuture.result().value().savedRevision));

    // Reopen project
    OpenProjectRequest openReq{.path = fixture.projectRoot};
    auto openFuture = OpenProjectUseCase(fixture.stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    QVERIFY(openFuture.result().has_value());
    domain::ComicProject reopenedProject = openFuture.result().value().project;

    // Verify reopened domain matches original
    const auto* reopenedRegion = reopenedProject.findPage(fixture.pageId)->findRegion(r1);
    QVERIFY(reopenedRegion != nullptr);
    QCOMPARE(reopenedRegion->source(), originalSource);
    QCOMPARE(reopenedRegion->translation(), originalTranslation);
    QVERIFY(reopenedRegion->translation() == originalTranslation);
    QCOMPARE(reopenedRegion->translation().manualText(), originalTranslation.manualText());
    QCOMPARE(reopenedRegion->translation().modelText(), originalTranslation.modelText());
    QCOMPARE(reopenedRegion->translation().effectiveText(), originalTranslation.effectiveText());
    QCOMPARE(reopenedRegion->translation().candidates().size(), originalTranslation.candidates().size());
    QVERIFY(reopenedRegion->translation().candidates() == originalTranslation.candidates());
}

QTEST_MAIN(TranslationScenarioTest)
#include "TranslationScenarioTest.moc"
