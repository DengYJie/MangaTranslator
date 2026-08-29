#include "ChatCompletionsProtocolTest.h"

#include "domain/model/LanguageCode.h"
#include "network/translation/ChatCompletionsProtocol.h"
#include "network/translation/ProviderConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace manga;
using namespace manga::domain;
using manga::network::ProviderConfig;
using manga::network::StructuredOutputMode;

namespace {

QString kRegionId = QStringLiteral("11111111-1111-4111-8111-111111111111");
QString kOtherRegionId = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QString kSourceText = QStringLiteral("こんにちは");

ProviderConfig config() {
    ProviderConfig config;
    config.providerId = QStringLiteral("openai-main");
    config.endpoint = QUrl(QStringLiteral("https://api.example.com/v1/chat/completions"));
    config.model = QStringLiteral("gpt-4o-mini");
    return config;
}

application::TranslationRequest request() {
    application::TranslationRequest request;
    request.sourceLanguage = languages::Japanese;
    request.targetLanguage = languages::SimplifiedChinese;
    request.readingDirection = ReadingDirection::RightToLeft;
    application::TranslationRegion region;
    region.regionId = RegionId::parse(kRegionId.toStdString()).value_or(RegionId::generate());
    region.sourceText = kSourceText;
    request.regions.push_back(region);

    GlossaryEntry enabled;
    enabled.source = "主人公";
    enabled.target = "主人公";
    enabled.category = "character";
    enabled.enabled = true;
    GlossaryEntry disabled = enabled;
    disabled.target = "主角";
    disabled.enabled = false;
    request.glossary.push_back(enabled);
    request.glossary.push_back(disabled);
    return request;
}

QByteArray itemJson(const QString& regionId,
                    const QString& sourceText,
                    const QString& translation,
                    const QVector<QString>& candidates = {}) {
    QJsonArray candidateArray;
    for (const auto& candidate : candidates) {
        candidateArray.push_back(candidate);
    }
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("regionId"), regionId},
                             {QStringLiteral("sourceText"), sourceText},
                             {QStringLiteral("translation"), translation},
                             {QStringLiteral("candidates"), candidateArray},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray payloadJson(const QByteArray& items) {
    const QJsonDocument parsed = QJsonDocument::fromJson(items);
    const QJsonArray array = parsed.isArray() ? parsed.array() : QJsonArray{parsed.object()};
    return QJsonDocument(QJsonObject{{QStringLiteral("items"), array}}).toJson(QJsonDocument::Compact);
}

QByteArray choiceJson(const QString& content, const QString& finishReason, bool refusal = false) {
    QJsonObject message;
    if (refusal) {
        message.insert(QStringLiteral("refusal"), QStringLiteral("I cannot help with that"));
    } else {
        message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        message.insert(QStringLiteral("content"), content);
    }
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("choices"),
                              QJsonArray{QJsonObject{{QStringLiteral("finish_reason"), finishReason},
                                                     {QStringLiteral("message"), message}}}},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray successBody(const QVector<QString>& candidates = {}) {
    return choiceJson(
        QString::fromUtf8(payloadJson(itemJson(kRegionId, kSourceText, QStringLiteral("你好"), candidates))),
        QStringLiteral("stop"));
}

QJsonObject parseObject(const QByteArray& body) {
    return QJsonDocument::fromJson(body).object();
}

} // namespace

void ChatCompletionsProtocolTest::requestCarriesPromptV1AndDeterministicPayload() {
    const QByteArray body = manga::network::buildChatCompletionsRequest(request(), config());
    const QJsonObject root = parseObject(body);

    QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(root.value(QStringLiteral("temperature")).toDouble(), 0.2);
    QCOMPARE(root.value(QStringLiteral("max_completion_tokens")).toInt(), 2048);

    const QJsonArray messages = root.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QVERIFY(messages.at(0)
                .toObject()
                .value(QStringLiteral("content"))
                .toString()
                .startsWith(QStringLiteral("You translate manga dialogue")));

    const QJsonObject userPayload =
        QJsonDocument::fromJson(messages.at(1).toObject().value(QStringLiteral("content")).toString().toUtf8())
            .object();
    QCOMPARE(userPayload.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(userPayload.value(QStringLiteral("sourceLanguage")).toString(), QStringLiteral("ja"));
    QCOMPARE(userPayload.value(QStringLiteral("targetLanguage")).toString(), QStringLiteral("zh-Hans"));
    QCOMPARE(userPayload.value(QStringLiteral("readingDirection")).toString(), QStringLiteral("right-to-left"));

    const QJsonArray regions = userPayload.value(QStringLiteral("regions")).toArray();
    QCOMPARE(regions.size(), 1);
    QCOMPARE(regions.at(0).toObject().value(QStringLiteral("regionId")).toString(), kRegionId);
    QCOMPARE(regions.at(0).toObject().value(QStringLiteral("sourceText")).toString(), kSourceText);
    QCOMPARE(regions.at(0).toObject().value(QStringLiteral("order")).toInt(), 0);

    const QJsonObject format = root.value(QStringLiteral("response_format")).toObject();
    QCOMPARE(format.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
    QCOMPARE(format.value(QStringLiteral("json_schema")).toObject().value(QStringLiteral("strict")).toBool(), true);
}

void ChatCompletionsProtocolTest::requestUsesJsonObjectModeWhenConfigured() {
    auto jsonMode = config();
    jsonMode.structuredOutputMode = StructuredOutputMode::JsonObject;
    const QJsonObject root = parseObject(manga::network::buildChatCompletionsRequest(request(), jsonMode));
    const QJsonObject format = root.value(QStringLiteral("response_format")).toObject();
    QCOMPARE(format.value(QStringLiteral("type")).toString(), QStringLiteral("json_object"));
    QVERIFY(!format.contains(QStringLiteral("json_schema")));
}

void ChatCompletionsProtocolTest::requestSkipsDisabledOrInvalidGlossary() {
    const QJsonObject root = parseObject(manga::network::buildChatCompletionsRequest(request(), config()));
    const QJsonObject userPayload = QJsonDocument::fromJson(root.value(QStringLiteral("messages"))
                                                                .toArray()
                                                                .at(1)
                                                                .toObject()
                                                                .value(QStringLiteral("content"))
                                                                .toString()
                                                                .toUtf8())
                                        .object();
    const QJsonArray glossary = userPayload.value(QStringLiteral("glossary")).toArray();
    QCOMPARE(glossary.size(), 1);
    QCOMPARE(glossary.at(0).toObject().value(QStringLiteral("category")).toString(), QStringLiteral("character"));
}

void ChatCompletionsProtocolTest::parsesStrictResponse() {
    const auto parsed = manga::network::parseChatCompletionsResponse(
        successBody({QStringLiteral("哈囉")}), request(), QStringLiteral("openai-main"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->providerId, QStringLiteral("openai-main"));
    QCOMPARE(parsed->items.size(), 1);
    QCOMPARE(parsed->items.at(0).translation, QStringLiteral("你好"));
    QCOMPARE(parsed->items.at(0).candidates.size(), 1);
}

void ChatCompletionsProtocolTest::reportsRefusal() {
    const auto parsed = manga::network::parseChatCompletionsResponse(
        choiceJson(QString(), QStringLiteral("stop"), true), request(), QStringLiteral("openai-main"));
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, QStringLiteral("protocol.refused"));
}

void ChatCompletionsProtocolTest::reportsTruncatedAndFilteredResponses() {
    const auto truncated = manga::network::parseChatCompletionsResponse(
        choiceJson(successBody(), QStringLiteral("length")), request(), QStringLiteral("openai-main"));
    QVERIFY(!truncated.has_value());
    QCOMPARE(truncated.error().code, QStringLiteral("protocol.incomplete_response"));

    const auto filtered = manga::network::parseChatCompletionsResponse(
        choiceJson(successBody(), QStringLiteral("content_filter")), request(), QStringLiteral("openai-main"));
    QVERIFY(!filtered.has_value());
    QCOMPARE(filtered.error().code, QStringLiteral("protocol.content_filtered"));
}

void ChatCompletionsProtocolTest::reportsMissingContent() {
    const auto empty = manga::network::parseChatCompletionsResponse(
        choiceJson(QString(), QStringLiteral("stop")), request(), QStringLiteral("openai-main"));
    QVERIFY(!empty.has_value());
    QCOMPARE(empty.error().code, QStringLiteral("protocol.missing_content"));
}

void ChatCompletionsProtocolTest::rejectsMalformedJson() {
    const auto notJson = manga::network::parseChatCompletionsResponse(
        QByteArrayLiteral("not json"), request(), QStringLiteral("openai-main"));
    QCOMPARE(notJson.error().code, QStringLiteral("protocol.invalid_json"));

    const auto contentNotJson = manga::network::parseChatCompletionsResponse(
        choiceJson(QStringLiteral("{oops"), QStringLiteral("stop")), request(), QStringLiteral("openai-main"));
    QCOMPARE(contentNotJson.error().code, QStringLiteral("protocol.invalid_json"));
}

void ChatCompletionsProtocolTest::rejectsUnknownKeyAndWrongTypes() {
    QByteArray item = itemJson(kRegionId, kSourceText, QStringLiteral("你好"));
    item.replace("}", QByteArrayLiteral(",\"extra\":\"x\"}"));
    const auto unknownKey = manga::network::parseChatCompletionsResponse(
        choiceJson(QString::fromUtf8(payloadJson(item)), QStringLiteral("stop")),
        request(),
        QStringLiteral("openai-main"));
    QCOMPARE(unknownKey.error().code, QStringLiteral("protocol.schema_mismatch"));

    const QByteArray wrongType = QByteArrayLiteral("{\"regionId\":\"") + kRegionId.toUtf8() +
                                 QByteArrayLiteral("\",\"sourceText\":\"") + kSourceText.toUtf8() +
                                 QByteArrayLiteral("\",\"translation\":42,\"candidates\":[]}");
    const auto typeError = manga::network::parseChatCompletionsResponse(
        choiceJson(QString::fromUtf8(payloadJson(wrongType)), QStringLiteral("stop")),
        request(),
        QStringLiteral("openai-main"));
    QCOMPARE(typeError.error().code, QStringLiteral("protocol.schema_mismatch"));
}

void ChatCompletionsProtocolTest::rejectsDuplicateIds() {
    const QByteArray items = QByteArrayLiteral("[") + itemJson(kRegionId, kSourceText, QStringLiteral("你好")) +
                             QByteArrayLiteral(",") + itemJson(kRegionId, kSourceText, QStringLiteral("你好")) +
                             QByteArrayLiteral("]");
    const auto parsed = manga::network::parseChatCompletionsResponse(
        choiceJson(QString::fromUtf8(payloadJson(items)), QStringLiteral("stop")),
        request(),
        QStringLiteral("openai-main"));
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, QStringLiteral("protocol.duplicate_id"));
}

void ChatCompletionsProtocolTest::rejectsTooManyCandidatesAndOversizeFields() {
    QVector<QString> candidates;
    for (int index = 0; index < 6; ++index) {
        candidates.push_back(QString::number(index));
    }
    const auto tooMany =
        manga::network::parseChatCompletionsResponse(successBody(candidates), request(), QStringLiteral("openai-main"));
    QCOMPARE(tooMany.error().code, QStringLiteral("protocol.schema_mismatch"));

    const QString oversize(70000, QLatin1Char('a'));
    const auto tooLong = manga::network::parseChatCompletionsResponse(
        choiceJson(QString::fromUtf8(payloadJson(itemJson(kRegionId, kSourceText, oversize))), QStringLiteral("stop")),
        request(),
        QStringLiteral("openai-main"));
    QCOMPARE(tooLong.error().code, QStringLiteral("protocol.schema_mismatch"));
}

void ChatCompletionsProtocolTest::rejectsOversizeBody() {
    const QByteArray oversize(manga::network::kMaxResponseBodyBytes + 1, 'a');
    const auto parsed =
        manga::network::parseChatCompletionsResponse(oversize, request(), QStringLiteral("openai-main"));
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, QStringLiteral("protocol.invalid_json"));
}

void ChatCompletionsProtocolTest::passesUnknownIdsThroughForPartialAdoption() {
    // An unknown id is not a structural error: the application layer decides per item, so the
    // adapter keeps the batch usable.
    const auto parsed = manga::network::parseChatCompletionsResponse(
        choiceJson(QString::fromUtf8(payloadJson(itemJson(kOtherRegionId, kSourceText, QStringLiteral("你好")))),
                   QStringLiteral("stop")),
        request(),
        QStringLiteral("openai-main"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->items.size(), 1);
    QCOMPARE(QString::fromStdString(parsed->items.at(0).regionId.toString()), kOtherRegionId);
}

#include "moc_ChatCompletionsProtocolTest.cpp"
