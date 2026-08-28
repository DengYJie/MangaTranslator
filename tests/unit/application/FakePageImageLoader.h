#pragma once

#include "application/ports/IPageImageLoader.h"

#include <QImage>
#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakePageImageLoader : public IPageImageLoader {
public:
    QImage nextImage;
    ApplicationError nextError;
    bool shouldFail{false};
    int callCount{0};

    QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest, OperationContext context) override {
        ++callCount;
        auto promise = std::make_shared<QPromise<Result<QImage>>>();
        auto future = promise->future();

        if (context.stopToken.stop_requested()) {
            promise->addResult(std::unexpected(makeApplicationError(ErrorCategory::Cancelled, "cancelled", "")));
        } else if (shouldFail) {
            promise->addResult(std::unexpected(nextError));
        } else {
            promise->addResult(nextImage);
        }
        promise->finish();
        return future;
    }
};

} // namespace manga::application::test
