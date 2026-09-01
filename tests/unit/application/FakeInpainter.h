#pragma once

#include "application/ports/IInpainter.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeInpainter : public IInpainter {
public:
    ClearResponse nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<Result<ClearResponse>> clear(ClearRequest request, OperationContext context) override {
        ++callCount;
        m_request = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<ClearResponse>>>();
        m_promise->start();
        auto future = m_promise->future();
        if (!deferred) {
            complete();
        }
        return future;
    }

    void complete() {
        if (!m_promise)
            return;

        if (m_context.stopToken.stop_requested()) {
            m_promise->addResult(std::unexpected(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.clear", "Clear was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            auto response = nextResponse;
            response.regionId = m_request.regionId;
            if (response.patch.isNull()) {
                const int w = m_request.region.width > 0 ? static_cast<int>(m_request.region.width) : 80;
                const int h = m_request.region.height > 0 ? static_cast<int>(m_request.region.height) : 80;
                response.patch = QImage(w, h, QImage::Format_RGBA8888);
                response.patch.fill(Qt::transparent);
                response.affectedBounds = m_request.region;
            }
            m_promise->setProgressRange(0, 1);
            m_promise->setProgressValueAndText(1, "clear");
            m_promise->addResult(response);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

    ClearRequest m_request;
    OperationContext m_context;

private:
    std::unique_ptr<QPromise<Result<ClearResponse>>> m_promise;
};

} // namespace manga::application::test
