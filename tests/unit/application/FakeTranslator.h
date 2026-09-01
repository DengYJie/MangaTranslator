#pragma once

#include "application/ports/ITranslator.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeTranslator : public ITranslator {
public:
    TranslationResponse nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<Result<TranslationResponse>> translate(TranslationRequest request, OperationContext context) override {
        ++callCount;
        m_request = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<TranslationResponse>>>();
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
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.translation", "Translation was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            m_promise->setProgressRange(0, 0); // Indeterminate progress for translation
            m_promise->setProgressValueAndText(1, "translate");
            auto response = nextResponse;
            if (response.items.isEmpty()) {
                for (const auto& region : m_request.regions) {
                    response.items.append({region.regionId, region.sourceText, QStringLiteral("translated"), {}});
                }
            }
            m_promise->addResult(response);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

    TranslationRequest m_request;
    OperationContext m_context;

private:
    std::unique_ptr<QPromise<Result<TranslationResponse>>> m_promise;
};

} // namespace manga::application::test
