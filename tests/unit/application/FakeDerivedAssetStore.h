#pragma once

#include "application/ports/IDerivedAssetStore.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeDerivedAssetStore : public IDerivedAssetStore {
public:
    QImage nextLayer; // returned by readDerivedLayer
    QString nextRelativePath{"assets/derived/committed/v1/layer.png"};
    ApplicationError nextError;
    bool shouldFailRead{false};
    bool shouldFailCommit{false};
    int readCount{0};
    int commitCount{0};
    CommitDerivedLayerRequest lastCommitRequest;

    [[nodiscard]] QFuture<Result<QImage>> readDerivedLayer(ReadDerivedLayerRequest request,
                                                           OperationContext context) override {
        ++readCount;
        QPromise<Result<QImage>> promise;
        auto future = promise.future();
        if (context.stopToken.stop_requested()) {
            promise.addResult(std::unexpected(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.layer_read", "Layer read was cancelled")));
        } else if (shouldFailRead) {
            promise.addResult(std::unexpected(nextError));
        } else {
            promise.addResult(nextLayer);
        }
        promise.finish();
        return future;
    }

    [[nodiscard]] QFuture<Result<CommitDerivedLayerResponse>> commitDerivedLayer(CommitDerivedLayerRequest request,
                                                                                 OperationContext context) override {
        ++commitCount;
        lastCommitRequest = request;
        QPromise<Result<CommitDerivedLayerResponse>> promise;
        auto future = promise.future();
        if (context.stopToken.stop_requested()) {
            promise.addResult(std::unexpected(makeApplicationError(
                ErrorCategory::Cancelled, "cancelled.layer_commit", "Layer commit was cancelled")));
        } else if (shouldFailCommit) {
            promise.addResult(std::unexpected(nextError));
        } else {
            promise.addResult(CommitDerivedLayerResponse{nextRelativePath});
        }
        promise.finish();
        return future;
    }
};

} // namespace manga::application::test
