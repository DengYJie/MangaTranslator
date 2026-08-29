#pragma once

#include "application/ports/IComicImporter.h"

#include <QPromise>

#include <memory>
#include <utility>

namespace manga::application::test {

class FakeComicImporter : public IComicImporter {
public:
    ImportImagesResult nextResponse;
    ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};
    int discardCallCount{0};
    int acknowledgeCallCount{0};
    ImportImagesRequest lastRequest;
    DiscardImportedAssetsRequest lastDiscardRequest;
    AcknowledgeImportedAssetsRequest lastAcknowledgeRequest;

    QFuture<Result<ImportImagesResult>> importImages(ImportImagesRequest request, OperationContext context) override {
        ++callCount;
        lastRequest = request;
        m_context = context;
        m_promise = std::make_unique<QPromise<Result<ImportImagesResult>>>();
        auto future = m_promise->future();
        if (!deferred) {
            complete();
        }
        return future;
    }

    QFuture<Result<void>> discardImportedAssets(DiscardImportedAssetsRequest request,
                                                OperationContext context) override {
        ++discardCallCount;
        lastDiscardRequest = std::move(request);
        QPromise<Result<void>> promise;
        auto future = promise.future();
        if (context.stopToken.stop_requested()) {
            promise.addResult(std::unexpected(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.import_cleanup", "Cleanup was cancelled")));
        } else if (shouldFail) {
            promise.addResult(std::unexpected(nextError));
        } else {
            promise.addResult({});
        }
        promise.finish();
        return future;
    }

    QFuture<Result<void>> acknowledgeImportedAssets(AcknowledgeImportedAssetsRequest request,
                                                    OperationContext context) override {
        ++acknowledgeCallCount;
        lastAcknowledgeRequest = std::move(request);
        QPromise<Result<void>> promise;
        auto future = promise.future();
        if (context.stopToken.stop_requested()) {
            promise.addResult(std::unexpected(makeApplicationError(
                ErrorCategory::Cancelled, "cancelled.import_cleanup", "Acknowledgement was cancelled")));
        } else if (shouldFail) {
            promise.addResult(std::unexpected(nextError));
        } else {
            promise.addResult({});
        }
        promise.finish();
        return future;
    }

    void complete() {
        if (!m_promise)
            return;

        if (m_context.stopToken.stop_requested()) {
            m_promise->addResult(std::unexpected(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.import", "Import was cancelled")));
        } else if (shouldFail) {
            m_promise->addResult(std::unexpected(nextError));
        } else {
            m_promise->setProgressRange(0, lastRequest.filePaths.size());
            m_promise->setProgressValueAndText(lastRequest.filePaths.size(), "import");
            m_promise->addResult(nextResponse);
        }
        m_promise->finish();
        m_promise.reset();
        m_context = {};
    }

private:
    OperationContext m_context;
    std::unique_ptr<QPromise<Result<ImportImagesResult>>> m_promise;
};

} // namespace manga::application::test
