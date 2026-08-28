#pragma once

#include "application/ports/IComicExporter.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeComicExporter : public IComicExporter {
public:
    ExportResponse nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};
    ExportRequest lastRequest;

    QFuture<Result<ExportResponse>> exportPages(ExportRequest request, OperationContext context) override {
        ++callCount;
        lastRequest = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<ExportResponse>>>();
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
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.export", "Export was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            m_promise->setProgressRange(0, lastRequest.pages.size());
            m_promise->setProgressValueAndText(lastRequest.pages.size(), "export");
            m_promise->addResult(nextResponse);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

private:
    OperationContext m_context;
    std::unique_ptr<QPromise<Result<ExportResponse>>> m_promise;
};

} // namespace manga::application::test
