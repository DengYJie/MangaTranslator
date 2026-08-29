#pragma once

#include "application/error/ApplicationError.h"
#include "network/translation/ChatCompletionsProtocol.h"

#include <QByteArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>

#include <atomic>
#include <functional>
#include <memory>

namespace manga::network {

struct HttpResponse {
    int httpStatus{0};
    QByteArray body;
    QNetworkReply::NetworkError networkError{QNetworkReply::NoError};
    // True once the request reached the remote side; a failure before that point may be
    // retried, a failure after it may already have been billed.
    bool requestBodySent{false};
    QByteArray retryAfter;
};

using HttpResponsePromise = QPromise<application::Result<HttpResponse>>;

/// Handle for one in-flight send. cancel() aborts the reply on the network thread.
class IPendingRequest {
public:
    virtual ~IPendingRequest() = default;
    virtual void cancel() = 0;
};

/// Transport seam. The production implementation keeps QNetworkAccessManager and every reply
/// on one event-loop thread; tests replace it with a deterministic fake.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    /// Runs task on the network thread. Credential reads, requests, backoff waits and parsing
    /// all run there, so no secret or reply crosses a thread boundary.
    virtual void post(std::function<void()> task) = 0;
    [[nodiscard]] virtual std::shared_ptr<IPendingRequest>
    send(const QNetworkRequest& request, const QByteArray& body, std::shared_ptr<HttpResponsePromise> promise) = 0;
    virtual void shutdown() = 0;
};

} // namespace manga::network
