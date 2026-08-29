#pragma once

#include "network/translation/IHttpTransport.h"

#include <QObject>
#include <QSet>
#include <QThread>

#include <atomic>
#include <memory>

namespace manga::network {
namespace detail {

/// Shared state for one in-flight reply. cancel() may run on any thread, so the reply pointer
/// is guarded; the reply itself is only touched on the network thread.
struct ReplyState {
    QMutex mutex;
    QNetworkReply* reply{nullptr};
    bool cancelled{false};
};

} // namespace detail

/// QNetworkAccessManager and every reply live on one dedicated event-loop thread; replies are
/// created, aborted and destroyed there. Only parsed immutable results leave this transport.
class QtHttpTransport final : public QObject, public IHttpTransport {
public:
    QtHttpTransport();
    ~QtHttpTransport() override;

    QtHttpTransport(const QtHttpTransport&) = delete;
    QtHttpTransport& operator=(const QtHttpTransport&) = delete;

    void post(std::function<void()> task) override;
    [[nodiscard]] std::shared_ptr<IPendingRequest>
    send(const QNetworkRequest& request, const QByteArray& body, std::shared_ptr<HttpResponsePromise> promise) override;
    void shutdown() override;

private:
    QThread* m_thread{nullptr};
    QObject* m_context{nullptr};
    QNetworkAccessManager* m_manager{nullptr};
    std::atomic<bool> m_shutdown{false};
    QSet<QNetworkReply*> m_activeReplies;
};

} // namespace manga::network
