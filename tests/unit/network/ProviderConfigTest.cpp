#include "ProviderConfigTest.h"

#include "network/translation/ProviderConfig.h"

#include <QtTest/QtTest>

using namespace manga;
using manga::network::EndpointPolicy;
using manga::network::ProviderConfig;

namespace {

ProviderConfig validConfig() {
    ProviderConfig config;
    config.providerId = QStringLiteral("openai-main");
    config.endpoint = QUrl(QStringLiteral("https://api.example.com/v1/chat/completions"));
    config.model = QStringLiteral("gpt-4o-mini");
    return config;
}

} // namespace

void ProviderConfigTest::acceptsHttpsEndpoint() {
    QVERIFY(network::validateProviderConfig(validConfig(), EndpointPolicy::HttpsOnly).has_value());
}

void ProviderConfigTest::rejectsPlainHttpByDefault() {
    auto config = validConfig();
    config.endpoint = QUrl(QStringLiteral("http://127.0.0.1:8123/v1/chat/completions"));
    const auto result = network::validateProviderConfig(config, EndpointPolicy::HttpsOnly);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_endpoint"));
}

void ProviderConfigTest::allowsLoopbackHttpOnlyWhenOptedIn() {
    auto loopback = validConfig();
    loopback.endpoint = QUrl(QStringLiteral("http://127.0.0.1:8123/v1/chat/completions"));
    QVERIFY(network::validateProviderConfig(loopback, EndpointPolicy::AllowLoopbackHttp).has_value());

    auto remote = validConfig();
    remote.endpoint = QUrl(QStringLiteral("http://api.example.com/v1/chat/completions"));
    QVERIFY(!network::validateProviderConfig(remote, EndpointPolicy::AllowLoopbackHttp).has_value());
}

void ProviderConfigTest::rejectsUserInfoAndFragment() {
    auto userInfo = validConfig();
    userInfo.endpoint = QUrl(QStringLiteral("https://user:secret@api.example.com/v1/chat/completions"));
    QVERIFY(!network::validateProviderConfig(userInfo, EndpointPolicy::HttpsOnly).has_value());

    auto fragment = validConfig();
    fragment.endpoint = QUrl(QStringLiteral("https://api.example.com/v1/chat/completions#frag"));
    QVERIFY(!network::validateProviderConfig(fragment, EndpointPolicy::HttpsOnly).has_value());
}

void ProviderConfigTest::rejectsInvalidProviderId() {
    for (const QString& bad : {QString(), QStringLiteral("OpenAI"), QString(QStringLiteral("a")).repeated(65)}) {
        auto config = validConfig();
        config.providerId = bad;
        const auto result = network::validateProviderConfig(config, EndpointPolicy::HttpsOnly);
        QVERIFY2(!result.has_value(), qPrintable(QStringLiteral("provider id '%1' was accepted").arg(bad)));
        QCOMPARE(result.error().code, QStringLiteral("input.invalid_provider"));
    }
}

void ProviderConfigTest::rejectsOutOfRangeValues() {
    auto shortTimeout = validConfig();
    shortTimeout.timeoutMs = 100;
    QCOMPARE(network::validateProviderConfig(shortTimeout, EndpointPolicy::HttpsOnly).error().code,
             QStringLiteral("input.invalid_timeout"));

    auto longTimeout = validConfig();
    longTimeout.timeoutMs = 600000;
    QCOMPARE(network::validateProviderConfig(longTimeout, EndpointPolicy::HttpsOnly).error().code,
             QStringLiteral("input.invalid_timeout"));

    auto tokens = validConfig();
    tokens.maxCompletionTokens = 0;
    QCOMPARE(network::validateProviderConfig(tokens, EndpointPolicy::HttpsOnly).error().code,
             QStringLiteral("input.invalid_token_limit"));

    auto temperature = validConfig();
    temperature.temperature = 3.0;
    QCOMPARE(network::validateProviderConfig(temperature, EndpointPolicy::HttpsOnly).error().code,
             QStringLiteral("input.invalid_temperature"));
}

#include "moc_ProviderConfigTest.cpp"
