#pragma once

#include "application/ports/ITextLayouter.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeTextLayouter : public ITextLayouter {
public:
    LayoutResponse nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<Result<LayoutResponse>> layout(LayoutRequest request, OperationContext context) override {
        ++callCount;
        m_request = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<LayoutResponse>>>();
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
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.layout", "Layout was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            auto response = nextResponse;
            response.regionId = m_request.regionId;
            m_promise->setProgressRange(0, 1);
            m_promise->setProgressValueAndText(1, "layout");
            m_promise->addResult(response);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

    LayoutRequest m_request;
    OperationContext m_context;

private:
    std::unique_ptr<QPromise<Result<LayoutResponse>>> m_promise;
};

} // namespace manga::application::test
