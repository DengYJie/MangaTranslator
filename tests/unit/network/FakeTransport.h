#pragma once

#include "network/translation/IHttpTransport.h"

#include <QVector>

#include <functional>
#include <memory>

namespace manga::network::test {

/// Deterministic transport: tasks run inline and queued responses complete immediately unless
/// a test defers completion to observe cancellation.
class FakeTransport final : public IHttpTransport {
public:
    struct PendingRequest final : public IPendingRequest {
        explicit PendingRequest(std::shared_ptr<HttpResponsePromise> promise) : promise(std::move(promise)) {}
        void cancel() override {
            cancelled = true;
            if (onCancel) {
                onCancel();
            }
        }
        std::shared_ptr<HttpResponsePromise> promise;
        std::function<void()> onCancel;
        bool cancelled{false};
    };

    QVector<HttpResponse> responses;
    QVector<QNetworkRequest> requests;
    QVector<QByteArray> requestBodies;
    bool deferCompletion{false};
    std::shared_ptr<PendingRequest> lastPending;
    int sendCount{0};

    void post(std::function<void()> task) override {
        task();
    }

    std::shared_ptr<IPendingRequest> send(const QNetworkRequest& request,
                                          const QByteArray& body,
                                          std::shared_ptr<HttpResponsePromise> promise) override {
        ++sendCount;
        requests.push_back(request);
        requestBodies.push_back(body);
        auto pending = std::make_shared<PendingRequest>(promise);
        lastPending = pending;
        if (deferCompletion) {
            return pending;
        }
        completeWith(pending, nextResponse());
        return pending;
    }

    void shutdown() override {}

    void completeDeferred(const HttpResponse& response) {
        if (lastPending != nullptr) {
            completeWith(lastPending, response);
        }
    }

    [[nodiscard]] bool lastRequestWasCancelled() const {
        return lastPending != nullptr && lastPending->cancelled;
    }

private:
    [[nodiscard]] HttpResponse nextResponse() {
        if (responses.isEmpty()) {
            return HttpResponse{};
        }
        return responses.takeFirst();
    }

    static void completeWith(const std::shared_ptr<PendingRequest>& pending, const HttpResponse& response) {
        pending->promise->addResult(application::Result<HttpResponse>(response));
        pending->promise->finish();
    }
};

} // namespace manga::network::test
