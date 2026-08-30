#pragma once

#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"

#include <QPromise>

#include <memory>

namespace manga::test {

/// Deferred-capable thumbnail store. `readHit` controls whether reads succeed;
/// misses drive the use case into the generate path (loader + writeThumbnail).
class FakeThumbnailStore final : public application::IThumbnailStore {
public:
    bool readHit{false};
    QImage readImage;
    int readCount{0};
    int writeCount{0};

    QFuture<application::Result<QImage>> readThumbnail(application::ReadThumbnailRequest,
                                                       application::OperationContext) override {
        ++readCount;
        if (readHit) {
            return application::makeReadyFuture<QImage>(readImage);
        }
        return application::makeFailedFuture<QImage>(
            application::makeApplicationError(application::ErrorCategory::Persistence,
                                              QStringLiteral("test.thumbnail_missing"),
                                              QStringLiteral("missing")));
    }
    QFuture<application::Result<void>> writeThumbnail(application::WriteThumbnailRequest,
                                                      application::OperationContext) override {
        ++writeCount;
        return application::makeReadyFuture<void>({});
    }
};

/// Deferred-capable page image loader: when `deferred` is true the test drives
/// completion through completeAll(); otherwise resolves immediately.
class FakePageImageLoader final : public application::IPageImageLoader {
public:
    QImage nextImage;
    application::ApplicationError nextError;
    bool shouldFail{false};
    bool deferred{false};
    int callCount{0};

    QFuture<application::Result<QImage>> loadPageImage(application::LoadPageImageRequest,
                                                       application::OperationContext context) override {
        ++callCount;
        auto promise = std::make_shared<QPromise<application::Result<QImage>>>();
        auto future = promise->future();
        if (deferred) {
            m_pending.append({std::move(promise), context});
            return future;
        }
        complete(promise, context);
        return future;
    }

    void completeAll() {
        const auto pending = std::exchange(m_pending, {});
        for (const auto& item : pending) {
            complete(item.promise, item.context);
        }
    }

    [[nodiscard]] int pendingCount() const {
        return static_cast<int>(m_pending.size());
    }

private:
    struct Pending {
        std::shared_ptr<QPromise<application::Result<QImage>>> promise;
        application::OperationContext context;
    };
    void complete(const std::shared_ptr<QPromise<application::Result<QImage>>>& promise,
                  const application::OperationContext& context) {
        promise->start();
        if (context.stopToken.stop_requested()) {
            promise->addResult(std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Cancelled, QStringLiteral("cancelled"), QStringLiteral("cancelled"))));
        } else if (shouldFail) {
            promise->addResult(std::unexpected(nextError));
        } else {
            promise->addResult(nextImage);
        }
        promise->finish();
    }
    QVector<Pending> m_pending;
};

} // namespace manga::test
