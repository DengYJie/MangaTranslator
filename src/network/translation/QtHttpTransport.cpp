#include "network/translation/QtHttpTransport.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QTimer>

#include <utility>

namespace manga::network {
namespace {

/// Aborts the reply on its own thread and never touches it afterwards.
class QtPendingRequest final : public IPendingRequest {
public:
    explicit QtPendingRequest(std::shared_ptr<detail::ReplyState> state) : m_state(std::move(state)) {}

    void cancel() override {
        QMutexLocker locker(&m_state->mutex);
        m_state->cancelled = true;
        if (m_state->reply != nullptr) {
            QMetaObject::invokeMethod(m_state->reply, &QNetworkReply::abort, Qt::QueuedConnection);
        }
    }

private:
    std::shared_ptr<detail::ReplyState> m_state;
};

} // namespace

QtHttpTransport::QtHttpTransport() {
    m_thread = new QThread;
    m_context = new QObject;
    m_context->moveToThread(m_thread);
    m_thread->start();
    // Blocking so the manager exists before the first send is posted.
    QMetaObject::invokeMethod(
        m_context, [this]() { m_manager = new QNetworkAccessManager(m_context); }, Qt::BlockingQueuedConnection);
}

QtHttpTransport::~QtHttpTransport() {
    shutdown();
    if (m_thread != nullptr) {
        if (QThread::currentThread() == m_thread) {
            m_context->deleteLater();
            m_thread->quit();
            QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        } else {
            m_thread->quit();
            m_thread->wait();
            delete m_context;
            delete m_thread;
        }
    }
}

void QtHttpTransport::shutdown() {
    // Exchange to ensure we only abort once if called multiple times.
    if (m_shutdown.exchange(true)) {
        return;
    }
    if (m_thread != nullptr && QThread::currentThread() == m_thread) {
        for (auto* reply : m_activeReplies) {
            reply->abort();
        }
    } else if (m_context != nullptr) {
        QMetaObject::invokeMethod(
            m_context,
            [this]() {
                for (auto* reply : m_activeReplies) {
                    reply->abort();
                }
            },
            Qt::BlockingQueuedConnection);
    }
}

void QtHttpTransport::post(std::function<void()> task) {
    if (m_shutdown.load()) {
        return;
    }
    QMetaObject::invokeMethod(m_context, std::move(task), Qt::QueuedConnection);
}

std::shared_ptr<IPendingRequest> QtHttpTransport::send(const QNetworkRequest& request,
                                                       const QByteArray& body,
                                                       std::shared_ptr<HttpResponsePromise> promise) {
    auto state = std::make_shared<detail::ReplyState>();
    auto pending = std::make_shared<QtPendingRequest>(state);
    if (m_shutdown.load()) {
        promise->addResult(std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Network,
                                              QStringLiteral("network.unavailable"),
                                              QStringLiteral("The network transport is not available"))));
        promise->finish();
        return pending;
    }
    post([this, request, body, promise, state]() {
        if (m_shutdown.load() || m_manager == nullptr) {
            promise->addResult(std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Network,
                                                  QStringLiteral("network.unavailable"),
                                                  QStringLiteral("The network transport is not available"))));
            promise->finish();
            return;
        }
        QNetworkReply* reply = nullptr;
        bool cancelledBeforePost = false;
        {
            QMutexLocker locker(&state->mutex);
            if (state->cancelled) {
                cancelledBeforePost = true;
            } else {
                reply = m_manager->post(request, body);
                m_activeReplies.insert(reply);
                state->reply = reply;
            }
        }
        if (cancelledBeforePost) {
            HttpResponse response;
            response.networkError = QNetworkReply::OperationCanceledError;
            promise->addResult(application::Result<HttpResponse>(response));
            promise->finish();
            return;
        }
        QObject::connect(reply, &QNetworkReply::finished, m_context, [this, reply, promise, state]() {
            m_activeReplies.remove(reply);
            HttpResponse response;
            const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            response.requestBodySent = status.isValid();
            response.httpStatus = status.isValid() ? status.toInt() : 0;
            response.networkError = reply->error();
            response.retryAfter = reply->rawHeader("Retry-After");
            // One byte past the cap makes an oversized response detectable.
            if (reply->isOpen()) {
                response.body = reply->read(kMaxResponseBodyBytes + 1);
            }
            reply->deleteLater();
            {
                QMutexLocker locker(&state->mutex);
                state->reply = nullptr;
            }
            promise->addResult(application::Result<HttpResponse>(response));
            promise->finish();
        });
    });
    return pending;
}

} // namespace manga::network
