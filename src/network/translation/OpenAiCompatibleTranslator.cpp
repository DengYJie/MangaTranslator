#include "network/translation/OpenAiCompatibleTranslator.h"

#include "network/translation/ChatCompletionsProtocol.h"
#include "network/translation/QtHttpTransport.h"

#include <QElapsedTimer>
#include <QNetworkRequest>
#include <QTimer>

#include <optional>
#include <stop_token>
#include <utility>

namespace manga::network {
namespace {

application::ApplicationError cancelledError() {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.user_requested"),
                                             QStringLiteral("The translation was cancelled"));
}

application::ApplicationError timeoutError() {
    return application::makeApplicationError(application::ErrorCategory::Network,
                                             QStringLiteral("network.timeout"),
                                             QStringLiteral("The request did not complete in time"),
                                             application::RecoveryAction::Retry);
}

application::ApplicationError translateUnavailable() {
    return application::makeApplicationError(application::ErrorCategory::Internal,
                                             QStringLiteral("internal.unavailable_dependency"),
                                             QStringLiteral("Translation service is unavailable"));
}

} // namespace

struct OpenAiCompatibleTranslator::Job : public std::enable_shared_from_this<Job> {
    ProviderConfig config;
    application::ICredentialStore* store{nullptr};
    std::shared_ptr<IHttpTransport> transport;
    RetryPolicy retryPolicy;
    EndpointPolicy endpointPolicy{EndpointPolicy::HttpsOnly};

    application::TranslationRequest request;
    application::OperationContext context;
    std::shared_ptr<QPromise<application::Result<application::TranslationResponse>>> promise;

    QNetworkRequest httpRequest;
    QByteArray body;
    int attempt{0};
    QElapsedTimer timer;

    QMutex mutex;
    bool settled{false};
    QTimer* retryTimer{nullptr};
    std::shared_ptr<IPendingRequest> pending;
    std::unique_ptr<std::stop_callback<std::function<void()>>> stopCallback;

    [[nodiscard]] int remainingDeadlineMs() const {
        const qint64 elapsed = timer.isValid() ? timer.elapsed() : 0;
        const int remaining = static_cast<int>(config.timeoutMs) - static_cast<int>(elapsed);
        return remaining > 0 ? remaining : 0;
    }

    void settle(application::Result<application::TranslationResponse> result) {
        QTimer* timerToCancel = nullptr;
        std::unique_ptr<std::stop_callback<std::function<void()>>> callbackToDestroy;
        {
            QMutexLocker locker(&mutex);
            if (settled) {
                return;
            }
            settled = true;
            timerToCancel = retryTimer;
            retryTimer = nullptr;
            pending.reset();
            callbackToDestroy = std::move(stopCallback);
        }
        callbackToDestroy.reset();
        if (timerToCancel != nullptr) {
            transport->post([timerToCancel]() {
                timerToCancel->stop();
                timerToCancel->deleteLater();
            });
        }
        promise->addResult(std::move(result));
        promise->finish();
    }

    void handleStopRequested() {
        std::shared_ptr<IPendingRequest> pendingToCancel;
        QTimer* timerToCancel = nullptr;
        {
            QMutexLocker locker(&mutex);
            if (settled) {
                return;
            }
            timerToCancel = retryTimer;
            retryTimer = nullptr;
            pendingToCancel = pending;
        }
        if (pendingToCancel != nullptr) {
            pendingToCancel->cancel();
        }
        if (timerToCancel != nullptr) {
            transport->post([timerToCancel]() {
                timerToCancel->stop();
                timerToCancel->deleteLater();
            });
        }
        // Settle even when neither request nor retry timer was published yet.
        // Their setup paths observe settled under the same mutex and cancel themselves.
        settle(std::unexpected(cancelledError()));
    }

    static void run(const std::shared_ptr<Job>& job) {
        if (const auto validation = validateProviderConfig(job->config, job->endpointPolicy); !validation) {
            job->settle(std::unexpected(validation.error()));
            return;
        }
        if (job->context.stopToken.stop_requested()) {
            job->settle(std::unexpected(cancelledError()));
            return;
        }

        auto key = job->store->readApiKey(job->config.providerId);
        if (!key) {
            job->settle(std::unexpected(key.error()));
            return;
        }

        QNetworkRequest request(normalizeChatCompletionsEndpoint(job->config.endpoint));
        // The media type, the Authorization field name and the Bearer prefix are spelled by the
        // Chat Completions contract, not chosen here, so they are not free to rename.
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        // Local providers (Ollama, LM Studio, llama.cpp server) often do not require an API key.
        // Only add the Authorization header when a key is stored; an empty key is not an error.
        if (!key->isEmpty()) {
            // The header copy is the only plaintext survivor and lives as long as this job.
            const QByteArray authorization = QByteArrayLiteral("Bearer ") + key->bytes();
            request.setRawHeader("Authorization", authorization);
        }
        job->httpRequest = request;
        job->body = buildChatCompletionsRequest(job->request, job->config);
        job->timer.start();

        if (job->context.stopToken.stop_possible()) {
            job->stopCallback = std::make_unique<std::stop_callback<std::function<void()>>>(
                job->context.stopToken, [job]() { job->handleStopRequested(); });
        }

        sendAttempt(job);
    }

    static void sendAttempt(const std::shared_ptr<Job>& job) {
        if (job->context.stopToken.stop_requested()) {
            job->settle(std::unexpected(cancelledError()));
            return;
        }
        const int remaining = job->remainingDeadlineMs();
        if (remaining <= 0) {
            job->settle(std::unexpected(timeoutError()));
            return;
        }

        job->httpRequest.setTransferTimeout(remaining);

        auto responsePromise = std::make_shared<HttpResponsePromise>();
        responsePromise->start();

        auto pendingReq = job->transport->send(job->httpRequest, job->body, responsePromise);
        {
            QMutexLocker locker(&job->mutex);
            if (job->settled || job->context.stopToken.stop_requested()) {
                if (pendingReq != nullptr) {
                    pendingReq->cancel();
                }
                locker.unlock();
                job->settle(std::unexpected(cancelledError()));
                return;
            }
            job->pending = pendingReq;
        }

        responsePromise->future().then(
            [job](application::Result<HttpResponse> response) { onResponse(job, std::move(response)); });
    }

    static void retryAfter(const std::shared_ptr<Job>& job, int delayMs) {
        if (delayMs <= 0) {
            sendAttempt(job);
            return;
        }

        const int remaining = job->remainingDeadlineMs();
        if (remaining <= delayMs) {
            job->settle(std::unexpected(timeoutError()));
            return;
        }

        {
            QMutexLocker locker(&job->mutex);
            if (job->settled) {
                return;
            }
            if (job->context.stopToken.stop_requested()) {
                locker.unlock();
                job->settle(std::unexpected(cancelledError()));
                return;
            }
        }

        job->transport->post([job, delayMs]() {
            auto* timer = new QTimer();
            timer->setSingleShot(true);
            {
                QMutexLocker locker(&job->mutex);
                if (job->settled || job->context.stopToken.stop_requested()) {
                    timer->deleteLater();
                    return;
                }
                job->retryTimer = timer;
            }
            QObject::connect(timer, &QTimer::timeout, timer, [job, timer]() {
                bool proceed = false;
                {
                    QMutexLocker locker(&job->mutex);
                    if (job->retryTimer == timer) {
                        job->retryTimer = nullptr;
                        proceed = !job->settled;
                    }
                }
                timer->deleteLater();
                if (proceed) {
                    if (job->context.stopToken.stop_requested()) {
                        job->settle(std::unexpected(cancelledError()));
                        return;
                    }
                    sendAttempt(job);
                }
            });
            timer->start(delayMs);
        });
    }

    static void onResponse(const std::shared_ptr<Job>& job, application::Result<HttpResponse> response) {
        {
            QMutexLocker locker(&job->mutex);
            job->pending.reset();
            if (job->settled) {
                return;
            }
        }

        if (!response) {
            job->settle(std::unexpected(response.error()));
            return;
        }
        const HttpResponse& received = *response;

        if (received.networkError != QNetworkReply::NoError) {
            if (job->context.stopToken.stop_requested() ||
                received.networkError == QNetworkReply::OperationCanceledError) {
                job->settle(std::unexpected(cancelledError()));
                return;
            }
            if (received.httpStatus == 0) {
                const auto failure =
                    classifyTransportFailure(static_cast<int>(received.networkError), received.requestBodySent);
                const auto decision =
                    job->retryPolicy.decide(failure, job->attempt, received.retryAfter, job->remainingDeadlineMs());
                if (decision.shouldRetry) {
                    ++job->attempt;
                    retryAfter(job, decision.delayMs);
                    return;
                }
                job->settle(std::unexpected(failure.error));
                return;
            }
        }

        if (received.httpStatus < 200 || received.httpStatus >= 300) {
            const auto failure = classifyHttpStatus(received.httpStatus);
            const auto decision =
                job->retryPolicy.decide(failure, job->attempt, received.retryAfter, job->remainingDeadlineMs());
            if (decision.shouldRetry) {
                ++job->attempt;
                retryAfter(job, decision.delayMs);
                return;
            }
            job->settle(std::unexpected(failure.error));
            return;
        }

        auto parsed = parseChatCompletionsResponse(received.body, job->request, job->config.providerId);
        if (!parsed) {
            job->settle(std::unexpected(parsed.error()));
            return;
        }
        job->settle(*parsed);
    }
};

OpenAiCompatibleTranslator::OpenAiCompatibleTranslator(std::shared_ptr<const ProviderConfig> config,
                                                       application::ICredentialStore* store,
                                                       std::unique_ptr<IHttpTransport> transport,
                                                       RetryPolicy retryPolicy,
                                                       EndpointPolicy endpointPolicy)
    : m_config(std::move(config)), m_store(store),
      m_transport(transport ? std::shared_ptr<IHttpTransport>(std::move(transport))
                            : std::make_shared<QtHttpTransport>()),
      m_retryPolicy(std::move(retryPolicy)), m_endpointPolicy(endpointPolicy) {}

OpenAiCompatibleTranslator::OpenAiCompatibleTranslator(ProviderConfig config,
                                                       application::ICredentialStore* store,
                                                       std::unique_ptr<IHttpTransport> transport,
                                                       RetryPolicy retryPolicy,
                                                       EndpointPolicy endpointPolicy)
    : OpenAiCompatibleTranslator(std::make_shared<const ProviderConfig>(std::move(config)),
                                 store,
                                 std::move(transport),
                                 std::move(retryPolicy),
                                 endpointPolicy) {}

OpenAiCompatibleTranslator::~OpenAiCompatibleTranslator() = default;

void OpenAiCompatibleTranslator::updateConfig(std::shared_ptr<const ProviderConfig> config) {
    m_config.store(std::move(config));
}

std::shared_ptr<const ProviderConfig> OpenAiCompatibleTranslator::config() const {
    return m_config.load();
}

QFuture<application::Result<application::TranslationResponse>>
OpenAiCompatibleTranslator::translate(application::TranslationRequest request, application::OperationContext context) {
    auto promise = std::make_shared<QPromise<application::Result<application::TranslationResponse>>>();
    promise->start();
    auto future = promise->future();

    if (m_store == nullptr || m_transport == nullptr) {
        promise->addResult(std::unexpected(translateUnavailable()));
        promise->finish();
        return future;
    }

    auto configSnapshot = m_config.load();
    if (!configSnapshot) {
        promise->addResult(std::unexpected(translateUnavailable()));
        promise->finish();
        return future;
    }

    auto job = std::make_shared<Job>();
    job->config = *configSnapshot;
    job->store = m_store;
    job->transport = m_transport;
    job->retryPolicy = m_retryPolicy;
    job->endpointPolicy = m_endpointPolicy;
    job->request = std::move(request);
    job->context = context;
    job->promise = promise;

    // The whole job runs on the transport thread so the credential read, the request and the
    // parse never hop between threads.
    m_transport->post([job]() { Job::run(job); });
    return future;
}
} // namespace manga::network
