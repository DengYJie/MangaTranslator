#include "OpenAiCompatibleTranslatorTest.h"

#include "FakeTransport.h"
#include "domain/model/LanguageCode.h"
#include "network/translation/ChatCompletionsProtocol.h"
#include "network/translation/OpenAiCompatibleTranslator.h"
#include "network/translation/ProviderConfig.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest/QtTest>

using namespace manga;
using namespace manga::domain;
using manga::network::EndpointPolicy;
using manga::network::HttpResponse;
using manga::network::OpenAiCompatibleTranslator;
using manga::network::ProviderConfig;
using manga::network::RetryPolicy;

namespace {

const QString kRegionId = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString kSourceText = QStringLiteral("こんにちは");
const QByteArray kTestKey = QByteArrayLiteral("sk-unit-test-not-a-real-key");

class StubCredentialStore final : public application::ICredentialStore {
public:
    explicit StubCredentialStore(bool hasKey) : m_hasKey(hasKey) {}

    application::Result<bool> hasApiKey(const QString&) override {
        ++hasCount;
        return m_hasKey;
    }
    application::Result<application::SecretBytes> readApiKey(const QString&) override {
        ++readCount;
        if (!m_hasKey) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Network,
                                                                     QStringLiteral("network.credentials_missing"),
                                                                     QStringLiteral("No API key is stored"),
                                                                     application::RecoveryAction::Reauthenticate));
        }
        return application::SecretBytes(kTestKey);
    }
    application::Result<void> writeApiKey(const QString&, application::SecretBytes) override {
        return {};
    }
    application::Result<void> deleteApiKey(const QString&) override {
        return {};
    }

    int hasCount{0};
    int readCount{0};

private:
    bool m_hasKey{true};
};

ProviderConfig providerConfig() {
    ProviderConfig config;
    config.providerId = QStringLiteral("openai-main");
    config.endpoint = QUrl(QStringLiteral("https://api.example.com/v1/chat/completions"));
    config.model = QStringLiteral("gpt-4o-mini");
    return config;
}

application::TranslationRequest translationRequest() {
    application::TranslationRequest request;
    request.sourceLanguage = languages::Japanese;
    request.targetLanguage = languages::SimplifiedChinese;
    request.readingDirection = ReadingDirection::RightToLeft;
    application::TranslationRegion region;
    region.regionId = RegionId::parse(kRegionId.toStdString()).value_or(RegionId::generate());
    region.sourceText = kSourceText;
    request.regions.push_back(region);
    return request;
}

QByteArray successPayload() {
    const QJsonObject item{
        {QStringLiteral("regionId"), kRegionId},
        {QStringLiteral("sourceText"), kSourceText},
        {QStringLiteral("translation"), QStringLiteral("你好")},
        {QStringLiteral("candidates"), QJsonArray{}},
    };
    const QByteArray content =
        QJsonDocument(QJsonObject{{QStringLiteral("items"), QJsonArray{item}}}).toJson(QJsonDocument::Compact);
    const QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                              {QStringLiteral("content"), QString::fromUtf8(content)}};
    const QJsonObject body{
        {QStringLiteral("choices"),
         QJsonArray{QJsonObject{{QStringLiteral("finish_reason"), QStringLiteral("stop")},
                                {QStringLiteral("message"), message}}}},
    };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

HttpResponse httpResponse(int status, const QByteArray& body = QByteArray()) {
    HttpResponse response;
    response.httpStatus = status;
    response.requestBodySent = true;
    response.body = body.isEmpty() ? successPayload() : body;
    return response;
}

bool waitForFinished(const QFuture<application::Result<application::TranslationResponse>>& future, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return future.isFinished();
}

/// Single-shot loopback server that answers the first request with a fixed body.
class LoopbackServer final : public QObject {
public:
    LoopbackServer() {
        connect(&m_server, &QTcpServer::newConnection, this, &LoopbackServer::onNewConnection);
    }
    ~LoopbackServer() override {
        m_server.close();
    }

    bool listen() {
        return m_server.listen(QHostAddress(QHostAddress::LocalHost), 0);
    }
    [[nodiscard]] quint16 port() const {
        return m_server.serverPort();
    }
    [[nodiscard]] QUrl endpoint(const QString& path = QStringLiteral("/v1/chat/completions")) const {
        return QUrl(QStringLiteral("http://%1:%2%3")
                        .arg(m_server.serverAddress().toString())
                        .arg(m_server.serverPort())
                        .arg(path));
    }

    void respondWith(const QByteArray& body) {
        m_body = body;
    }

    void setDeferResponse(bool defer) {
        m_deferResponse = defer;
    }

    [[nodiscard]] bool waitForRequest(int timeoutMs) const {
        QElapsedTimer timer;
        timer.start();
        while (!m_requestCompleted && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
        return m_requestCompleted;
    }

    void sendDeferredResponse() {
        if (m_activeSocket != nullptr && m_activeSocket->isOpen()) {
            const QByteArray response =
                QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
                QByteArray::number(m_body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_body;
            m_activeSocket->write(response);
            m_activeSocket->flush();
            m_activeSocket->disconnectFromHost();
        }
    }

    [[nodiscard]] const QByteArray& request() const {
        return m_request;
    }

private:
    void onNewConnection() {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            return;
        }
        m_activeSocket = socket;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            m_request.append(socket->readAll());
            const int headerEnd = m_request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            int contentLength = 0;
            const QByteArray header = m_request.left(headerEnd);
            for (const QByteArray& rawLine : header.split('\n')) {
                const QByteArray line = rawLine.trimmed();
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(15).trimmed().toInt();
                }
            }
            const int bodyStart = headerEnd + 4;
            if (m_request.size() - bodyStart < contentLength) {
                return;
            }
            m_requestCompleted = true;
            if (!m_deferResponse) {
                sendDeferredResponse();
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (m_activeSocket == socket) {
                m_activeSocket = nullptr;
            }
            socket->deleteLater();
        });
    }

    QTcpServer m_server;
    QTcpSocket* m_activeSocket{nullptr};
    QByteArray m_body;
    QByteArray m_request;
    bool m_requestCompleted{false};
    bool m_deferResponse{false};
};

} // namespace

void OpenAiCompatibleTranslatorTest::translatesSuccessfullyAndKeepsProviderIdentity() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(200));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(future.result().has_value());
    QCOMPARE(future.result()->providerId, QStringLiteral("openai-main"));
    QCOMPARE(future.result()->items.size(), 1);
    QCOMPARE(raw->sendCount, 1);
    QCOMPARE(store.readCount, 1);
}

void OpenAiCompatibleTranslatorTest::sendsBearerAuthorizationButNeverTheKeyInTheBody() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(200));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QCOMPARE(raw->requests.size(), 1);
    QCOMPARE(raw->requests.at(0).rawHeader("Authorization"), QByteArrayLiteral("Bearer ") + kTestKey);
    QVERIFY(!raw->requestBodies.at(0).contains(kTestKey));
    QVERIFY(!raw->requestBodies.at(0).contains("Authorization"));
}

void OpenAiCompatibleTranslatorTest::missingCredentialIsReportedWithoutSending() {
    StubCredentialStore store(false);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("network.credentials_missing"));
    QCOMPARE(raw->sendCount, 0);
}

void OpenAiCompatibleTranslatorTest::serverErrorIsReportedWithoutRetry() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(500));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("network.server_error"));
    QCOMPARE(raw->sendCount, 1);
}

void OpenAiCompatibleTranslatorTest::rateLimitedRequestRetriesOnceThenSucceeds() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    HttpResponse limited = httpResponse(429);
    limited.retryAfter = QByteArrayLiteral("0");
    transport->responses.push_back(limited);
    transport->responses.push_back(httpResponse(200));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(future.result().has_value());
    QCOMPARE(raw->sendCount, 2);
}

void OpenAiCompatibleTranslatorTest::unauthorizedIsNotRetried() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(401));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QCOMPARE(future.result().error().code, QStringLiteral("network.unauthorized"));
    QCOMPARE(raw->sendCount, 1);
}

void OpenAiCompatibleTranslatorTest::cancellationBeforeStartSkipsTheRequest() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(200));
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    std::stop_source source;
    source.request_stop();
    application::OperationContext context;
    context.stopToken = source.get_token();

    auto future = translator.translate(translationRequest(), context);
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(!future.result().has_value());
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(raw->sendCount, 0);
}

void OpenAiCompatibleTranslatorTest::cancellationAbortsTheInFlightRequest() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->deferCompletion = true;
    auto* raw = transport.get();
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    std::stop_source source;
    application::OperationContext context;
    context.stopToken = source.get_token();

    auto future = translator.translate(translationRequest(), context);
    QVERIFY(raw->lastPending != nullptr);

    source.request_stop();
    QVERIFY(raw->lastRequestWasCancelled());

    HttpResponse aborted;
    aborted.networkError = QNetworkReply::OperationCanceledError;
    raw->completeDeferred(aborted);

    QVERIFY(waitForFinished(future, 5000));
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(future.result().error().code, QStringLiteral("cancelled.user_requested"));
}

void OpenAiCompatibleTranslatorTest::oversizeResponseIsRejected() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->responses.push_back(httpResponse(200, QByteArray(manga::network::kMaxResponseBodyBytes + 1, 'a')));
    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("protocol.invalid_json"));
}

void OpenAiCompatibleTranslatorTest::loopbackServerRoundTripUsesTheQtTransport() {
    LoopbackServer server;
    server.respondWith(successPayload());
    QVERIFY(server.listen());

    auto config = providerConfig();
    config.endpoint = server.endpoint();
    config.timeoutMs = 5000;

    StubCredentialStore store(true);
    // No transport injected: the production QNetworkAccessManager transport runs on its own
    // thread with the reply created, aborted and destroyed there.
    OpenAiCompatibleTranslator translator(
        config, &store, nullptr, RetryPolicy(nullptr, nullptr), EndpointPolicy::AllowLoopbackHttp);

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 10000));

    QVERIFY(server.waitForRequest(5000));
    QVERIFY(server.request().contains("Authorization: Bearer "));
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result()->items.size(), 1);
}

void OpenAiCompatibleTranslatorTest::loopbackServerRejectsOversizeBody() {
    LoopbackServer server;
    server.respondWith(QByteArray(manga::network::kMaxResponseBodyBytes + 4096, 'a'));
    QVERIFY(server.listen());

    auto config = providerConfig();
    config.endpoint = server.endpoint();
    config.timeoutMs = 5000;

    StubCredentialStore store(true);
    OpenAiCompatibleTranslator translator(
        config, &store, nullptr, RetryPolicy(nullptr, nullptr), EndpointPolicy::AllowLoopbackHttp);

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 10000));

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("protocol.invalid_json"));
}

void OpenAiCompatibleTranslatorTest::loopbackInFlightCancelUsesRealQtHttpTransport() {
    LoopbackServer server;
    server.respondWith(successPayload());
    server.setDeferResponse(true);
    QVERIFY(server.listen());

    auto config = providerConfig();
    config.endpoint = server.endpoint();
    config.timeoutMs = 5000;

    StubCredentialStore store(true);
    OpenAiCompatibleTranslator translator(
        config, &store, nullptr, RetryPolicy(nullptr, nullptr), EndpointPolicy::AllowLoopbackHttp);

    std::stop_source source;
    application::OperationContext context;
    context.stopToken = source.get_token();

    auto future = translator.translate(translationRequest(), context);

    // Wait until the real network thread sends the request and the loopback server receives it.
    QVERIFY(server.waitForRequest(5000));

    // Request is currently in flight. Cancel it now!
    source.request_stop();

    // The future must finish quickly without hanging.
    QVERIFY(waitForFinished(future, 2000));
    QVERIFY(!future.result().has_value());
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(future.result().error().code, QStringLiteral("cancelled.user_requested"));
}

void OpenAiCompatibleTranslatorTest::loopbackTranslatorDestructionWhileJobIsInFlightUsesRealQtHttpTransport() {
    LoopbackServer server;
    server.respondWith(successPayload());
    server.setDeferResponse(true);
    QVERIFY(server.listen());

    auto config = providerConfig();
    config.endpoint = server.endpoint();
    config.timeoutMs = 5000;

    StubCredentialStore store(true);
    QFuture<application::Result<application::TranslationResponse>> future;
    {
        OpenAiCompatibleTranslator translator(
            config, &store, nullptr, RetryPolicy(nullptr, nullptr), EndpointPolicy::AllowLoopbackHttp);
        future = translator.translate(translationRequest(), {});
    }
    // Translator is destroyed on this thread while request is in flight on the network thread.

    // Wait until the loopback server receives the request from the background network thread.
    QVERIFY(server.waitForRequest(5000));

    // Now send the response from the server!
    server.sendDeferredResponse();

    // The future must finish successfully on the network thread without crashing.
    QVERIFY(waitForFinished(future, 5000));
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result()->items.size(), 1);
    QCOMPARE(future.result()->providerId, QStringLiteral("openai-main"));
}

void OpenAiCompatibleTranslatorTest::cancellationDuringRetryBackoffAbortsImmediately() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    HttpResponse limited = httpResponse(429);
    // Backoff delay of 10 seconds (10000ms)
    limited.retryAfter = QByteArrayLiteral("10");
    transport->responses.push_back(limited);
    auto* raw = transport.get();

    OpenAiCompatibleTranslator translator(
        providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    std::stop_source source;
    application::OperationContext context;
    context.stopToken = source.get_token();

    QElapsedTimer elapsed;
    elapsed.start();
    auto future = translator.translate(translationRequest(), context);

    // Verify first attempt was sent and backoff timer is armed
    QCOMPARE(raw->sendCount, 1);
    QVERIFY(!future.isFinished());

    // Cancel while in backoff
    source.request_stop();

    // Must finish quickly without waiting for the 10-second backoff timer!
    QVERIFY(waitForFinished(future, 1000));
    QVERIFY(elapsed.elapsed() < 3000);

    QVERIFY(!future.result().has_value());
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(future.result().error().code, QStringLiteral("cancelled.user_requested"));
    QCOMPARE(raw->sendCount, 1);
}

void OpenAiCompatibleTranslatorTest::translatorDestructionWhileJobIsInFlightIsSafe() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    transport->deferCompletion = true;
    auto* raw = transport.get();

    QFuture<application::Result<application::TranslationResponse>> future;
    {
        OpenAiCompatibleTranslator translator(
            providerConfig(), &store, std::move(transport), RetryPolicy(nullptr, nullptr));
        future = translator.translate(translationRequest(), {});
    }
    // Translator was destroyed while request was in-flight.
    QVERIFY(raw->lastPending != nullptr);

    // Complete the deferred response now that translator is gone
    raw->completeDeferred(httpResponse(200));

    QVERIFY(waitForFinished(future, 5000));
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result()->providerId, QStringLiteral("openai-main"));
}

void OpenAiCompatibleTranslatorTest::retryIsSkippedWhenRetryAfterExceedsDeadline() {
    StubCredentialStore store(true);
    auto transport = std::make_unique<manga::network::test::FakeTransport>();
    HttpResponse limited = httpResponse(429);
    limited.retryAfter = QByteArrayLiteral("10"); // 10s wait > 5s deadline
    transport->responses.push_back(limited);
    auto* raw = transport.get();

    auto config = providerConfig();
    config.timeoutMs = 5000; // valid minimum 5s deadline, less than 10s retry-after

    OpenAiCompatibleTranslator translator(config, &store, std::move(transport), RetryPolicy(nullptr, nullptr));

    auto future = translator.translate(translationRequest(), {});
    QVERIFY(waitForFinished(future, 5000));

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("network.rate_limited"));
    QCOMPARE(raw->sendCount, 1);
}

#include "moc_OpenAiCompatibleTranslatorTest.cpp"
