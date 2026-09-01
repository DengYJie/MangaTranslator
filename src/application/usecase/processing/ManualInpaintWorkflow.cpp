#include "ManualInpaintWorkflow.h"

#include "application/operation/AsyncResult.h"

#include <QColorSpace>
#include <QPainter>

#include <memory>
#include <optional>

namespace manga::application {
namespace {
ApplicationError error(ErrorCategory category, const char* code, const char* message) {
    return makeApplicationError(category, QString::fromUtf8(code), QString::fromUtf8(message));
}
} // namespace

QFuture<Result<PreparedPageInpaintChange>> ManualInpaintWorkflow::execute(ManualPageInpaintRequest request) {
    if (m_store == nullptr || m_inpainter == nullptr)
        return makeFailedFuture<PreparedPageInpaintChange>(
            error(ErrorCategory::Internal, "internal.unavailable_dependency", "Inpaint dependency is unavailable"));
    if (!request.projectId.isValid() || !request.pageId.isValid() || request.pageImage.isNull() ||
        request.mask.isNull() || request.pageImage.size() != request.mask.size() || !request.dirtyBounds.isValid() ||
        !request.pageImage.rect().contains(request.dirtyBounds) || request.expectedPageSize.isValid() == false ||
        request.pageImage.size() != request.expectedPageSize ||
        request.expectedSessionGeneration != request.currentSessionGeneration ||
        request.context.stopToken.stop_requested())
        return makeFailedFuture<PreparedPageInpaintChange>(error(
            ErrorCategory::Input, "input.invalid_page_inpaint_request", "Page inpaint request is invalid or stale"));

    struct Job {
        IDerivedAssetStore* store;
        IPageInpainter* inpainter;
        ManualPageInpaintRequest request;
        QString beforeRef;
        QPromise<Result<PreparedPageInpaintChange>> promise;
    };
    auto job = std::make_shared<Job>(
        Job{m_store, m_inpainter, std::move(request), QString(), QPromise<Result<PreparedPageInpaintChange>>()});
    job->beforeRef = job->request.expectedClearLayerRef;
    job->promise.start();
    auto future = job->promise.future();

    QFuture<Result<QImage>> layerFuture;
    if (job->beforeRef.isEmpty()) {
        QImage transparent(job->request.expectedPageSize, QImage::Format_RGBA8888);
        transparent.fill(0);
        transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
        layerFuture = makeReadyFuture<QImage>(std::move(transparent));
    } else {
        ReadDerivedLayerRequest read{job->request.projectId,
                                     job->request.projectRoot,
                                     job->request.pageId,
                                     DerivedLayerKind::Clear,
                                     job->beforeRef};
        layerFuture = job->store->readDerivedLayer(std::move(read), job->request.context);
    }

    layerFuture.then([job](Result<QImage> current) {
        if (!current) {
            job->promise.addResult(std::unexpected(current.error()));
            job->promise.finish();
            return;
        }
        if (job->request.context.stopToken.stop_requested()) {
            job->promise.addResult(std::unexpected(error(
                ErrorCategory::Cancelled, "cancelled.stale_clear_layer", "Clear layer changed during inpainting")));
            job->promise.finish();
            return;
        }
        if ((*current).size() != job->request.expectedPageSize) {
            job->promise.addResult(std::unexpected(
                error(ErrorCategory::Input, "clear.layer_read_failed", "Stored clear layer has an invalid size")));
            job->promise.finish();
            return;
        }
        QImage layer = *current;
        if (layer.format() != QImage::Format_RGBA8888)
            layer = layer.convertToFormat(QImage::Format_RGBA8888);
        QImage source = job->request.pageImage.convertToFormat(QImage::Format_RGBA8888);
        QPainter painter(&source);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(QPoint(), layer);
        painter.end();

        PageInpaintRequest inpaint{job->request.projectId,
                                   job->request.pageId,
                                   std::move(source),
                                   job->request.mask,
                                   job->request.dirtyBounds};
        job->inpainter->inpaint(std::move(inpaint), job->request.context)
            .then([job, layer](Result<PageInpaintResponse> response) mutable {
                if (!response) {
                    job->promise.addResult(std::unexpected(response.error()));
                    job->promise.finish();
                    return;
                }
                if (job->request.context.stopToken.stop_requested() || response->projectId != job->request.projectId ||
                    response->pageId != job->request.pageId ||
                    response->image.size() != job->request.expectedPageSize) {
                    job->promise.addResult(std::unexpected(
                        error(ErrorCategory::Cancelled, "cancelled.stale_clear_layer", "Inpaint result is stale")));
                    job->promise.finish();
                    return;
                }
                QImage result = response->image.convertToFormat(QImage::Format_RGBA8888);
                QImage mask = job->request.mask.convertToFormat(QImage::Format_Grayscale8);
                for (int y = 0; y < result.height(); ++y) {
                    auto* dst = layer.scanLine(y);
                    const auto* src = result.constScanLine(y);
                    const auto* m = mask.constScanLine(y);
                    for (int x = 0; x < result.width(); ++x) {
                        if (m[x] != 0 && job->request.dirtyBounds.contains(x, y)) {
                            dst[x * 4] = src[x * 4];
                            dst[x * 4 + 1] = src[x * 4 + 1];
                            dst[x * 4 + 2] = src[x * 4 + 2];
                            dst[x * 4 + 3] = 255;
                        }
                    }
                }
                layer.setColorSpace(QColorSpace(QColorSpace::SRgb));
                CommitDerivedLayerRequest commit{job->request.projectId,
                                                 job->request.projectRoot,
                                                 job->request.pageId,
                                                 DerivedLayerKind::Clear,
                                                 std::move(layer)};
                job->store->commitDerivedLayer(std::move(commit), job->request.context)
                    .then([job](Result<CommitDerivedLayerResponse> committed) {
                        if (!committed) {
                            job->promise.addResult(std::unexpected(committed.error()));
                            job->promise.finish();
                            return;
                        }
                        if (job->request.context.stopToken.stop_requested()) {
                            job->promise.addResult(std::unexpected(error(ErrorCategory::Cancelled,
                                                                         "cancelled.stale_clear_layer",
                                                                         "Clear layer changed before commit")));
                            job->promise.finish();
                            return;
                        }
                        job->promise.addResult(PreparedPageInpaintChange{job->request.projectId,
                                                                         job->request.pageId,
                                                                         job->beforeRef,
                                                                         committed->relativePath,
                                                                         job->request.expectedSessionGeneration,
                                                                         job->request.expectedPageSize});
                        job->promise.finish();
                    });
            });
    });
    return future;
}
} // namespace manga::application
