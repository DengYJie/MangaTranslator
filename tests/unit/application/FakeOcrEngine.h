#pragma once

#include "application/ports/IOcrEngine.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeOcrEngine : public IOcrEngine {
public:
    OcrResponse nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<Result<OcrResponse>> recognize(OcrRequest request, OperationContext context) override {
        ++callCount;
        m_request = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<OcrResponse>>>();
        m_promise->start();
        auto future = m_promise->future();
        if (!deferred) {
            complete();
        }
        return future;
    }

    void complete() {
        if (m_promise == nullptr) {
            return;
        }
        if (m_context.stopToken.stop_requested()) {
            m_promise->addResult(
                std::unexpected(makeApplicationError(ErrorCategory::Cancelled, "cancelled.ocr", "OCR was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            auto response = nextResponse;
            response.regionId = m_request.regionId;
            if (response.text.isEmpty()) {
                response.text = QStringLiteral("recognized");
            }
            m_promise->setProgressRange(0, 1);
            m_promise->setProgressValueAndText(1, "ocr");
            m_promise->addResult(response);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

private:
    OcrRequest m_request;
    OperationContext m_context;
    std::unique_ptr<QPromise<Result<OcrResponse>>> m_promise;
};

} // namespace manga::application::test
