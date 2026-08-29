#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"

#include <QFuture>
#include <QImage>
#include <QPromise>

#include <memory>
#include <utility>

namespace manga::application {

struct LoadOrGenerateThumbnailRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    domain::PageId pageId;
    QString originalAssetPath;
    QSize originalSize;
    QSize targetSize{256, 256}; // Standard thumbnail bounding box
};

class LoadOrGenerateThumbnailUseCase {
public:
    LoadOrGenerateThumbnailUseCase(IThumbnailStore* store, IPageImageLoader* loader)
        : m_store(store), m_loader(loader) {}

    [[nodiscard]] QFuture<Result<QImage>> execute(const LoadOrGenerateThumbnailRequest& request,
                                                  const OperationContext& context) {
        if (!m_store || !m_loader) {
            return makeFailedFuture<QImage>(makeApplicationError(ErrorCategory::Internal,
                                                                 QStringLiteral("internal.null_port"),
                                                                 QStringLiteral("Ports are unavailable")));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<QImage>(makeApplicationError(ErrorCategory::Cancelled,
                                                                 QStringLiteral("cancelled.before_start"),
                                                                 QStringLiteral("Thumbnail loading was cancelled")));
        }

        if (!request.projectId.isValid() || request.projectRoot.trimmed().isEmpty() || !request.pageId.isValid() ||
            request.originalAssetPath.trimmed().isEmpty() || !request.originalSize.isValid() ||
            !request.targetSize.isValid()) {
            return makeFailedFuture<QImage>(makeApplicationError(ErrorCategory::Input,
                                                                 QStringLiteral("input.invalid_request"),
                                                                 QStringLiteral("Thumbnail request is invalid")));
        }

        auto promise = std::make_shared<QPromise<Result<QImage>>>();
        promise->start();
        auto future = promise->future();

        ReadThumbnailRequest readReq{request.projectId, request.projectRoot.trimmed(), request.pageId};
        auto readFuture = m_store->readThumbnail(std::move(readReq), context);

        readFuture.then([this, request, context, promise](Result<QImage> readResult) {
            if (readResult.has_value() && !readResult.value().isNull()) {
                promise->addResult(readResult.value());
                promise->finish();
                return;
            }

            if (context.stopToken.stop_requested()) {
                promise->addResult(
                    std::unexpected(makeApplicationError(ErrorCategory::Cancelled,
                                                         QStringLiteral("cancelled.during_generation"),
                                                         QStringLiteral("Thumbnail generation was cancelled"))));
                promise->finish();
                return;
            }

            // Generate thumbnail
            LoadPageImageRequest loadReq{
                request.projectId,
                request.projectRoot.trimmed(),
                request.pageId,
                request.originalAssetPath.trimmed(),
                domain::ImageSize{request.originalSize.width(), request.originalSize.height()}};

            m_loader->loadPageImage(std::move(loadReq), context)
                .then([this, request, context, promise](Result<QImage> loadResult) {
                    if (!loadResult.has_value()) {
                        promise->addResult(std::unexpected(loadResult.error()));
                        promise->finish();
                        return;
                    }

                    if (context.stopToken.stop_requested()) {
                        promise->addResult(std::unexpected(
                            makeApplicationError(ErrorCategory::Cancelled,
                                                 QStringLiteral("cancelled.after_load"),
                                                 QStringLiteral("Thumbnail generation was cancelled"))));
                        promise->finish();
                        return;
                    }

                    QImage thumbnail =
                        loadResult.value().scaled(request.targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                    WriteThumbnailRequest writeReq{
                        request.projectId, request.projectRoot.trimmed(), request.pageId, thumbnail};

                    m_store->writeThumbnail(std::move(writeReq), context).then([thumbnail, promise](Result<void>) {
                        promise->addResult(thumbnail);
                        promise->finish();
                    });
                });
        });

        return future;
    }

private:
    IThumbnailStore* m_store;
    IPageImageLoader* m_loader;
};

} // namespace manga::application
