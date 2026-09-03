#include "ManualPaintWorkflow.h"

#include "application/operation/AsyncResult.h"
#include "rendering/paint/StrokePaintRasterizer.h"

#include <QColorSpace>
#include <QDebug>

#include <memory>

namespace manga::application {
namespace {
ApplicationError makeError(ErrorCategory category, const char* code, const char* message) {
    return makeApplicationError(category, QString::fromUtf8(code), QString::fromUtf8(message));
}
} // namespace

QFuture<Result<PreparedPageManualOverrideChange>> ManualPaintWorkflow::execute(ManualPagePaintRequest request) {
    if (m_store == nullptr) {
        return makeFailedFuture<PreparedPageManualOverrideChange>(
            makeError(ErrorCategory::Internal, "internal.unavailable_dependency", "Derived store is unavailable"));
    }
    if (!request.projectId.isValid() || !request.pageId.isValid() || request.points.isEmpty() ||
        !request.expectedPageSize.isValid() || request.expectedPageSize.width() <= 0 ||
        request.expectedPageSize.height() <= 0 || request.diameter <= 0.0 ||
        request.expectedSessionGeneration != request.currentSessionGeneration ||
        request.context.stopToken.stop_requested()) {
        return makeFailedFuture<PreparedPageManualOverrideChange>(
            makeError(ErrorCategory::Input, "input.invalid_manual_paint_request", "Manual paint request is invalid"));
    }

    struct Job {
        IDerivedAssetStore* store{nullptr};
        ManualPagePaintRequest request;
        QString beforeRef;
        QPromise<Result<PreparedPageManualOverrideChange>> promise;
    };
    auto job = std::make_shared<Job>(Job{m_store, std::move(request), QString(), {}});
    job->beforeRef = job->request.expectedManualLayerRef;
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
                                     DerivedLayerKind::ManualOverride,
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
            job->promise.addResult(std::unexpected(makeError(
                ErrorCategory::Cancelled, "cancelled.stale_manual_layer", "Manual layer changed during painting")));
            job->promise.finish();
            return;
        }
        if (current->size() != job->request.expectedPageSize) {
            job->promise.addResult(std::unexpected(makeError(
                ErrorCategory::Input, "manual.layer_read_failed", "Stored manual layer has an invalid size")));
            job->promise.finish();
            return;
        }

        QImage layer = *current;
        if (layer.format() != QImage::Format_RGBA8888) {
            layer = layer.convertToFormat(QImage::Format_RGBA8888);
        }
        layer.setColorSpace(QColorSpace(QColorSpace::SRgb));

        // The stroke compositing reads each pixel as straight alpha, which the RGBA8888 and sRGB
        // normalization above establishes.
        manga::rendering::StrokePaintParams strokeParams;
        strokeParams.mode = job->request.mode == ManualStrokeMode::Paint ? manga::rendering::StrokePaintMode::Paint
                                                                         : manga::rendering::StrokePaintMode::Erase;
        strokeParams.color = job->request.color;
        strokeParams.diameter = job->request.diameter;
        manga::rendering::StrokePaintRasterizer::rasterizeStroke(layer, strokeParams, job->request.points);

        CommitDerivedLayerRequest commitReq{job->request.projectId,
                                            job->request.projectRoot,
                                            job->request.pageId,
                                            DerivedLayerKind::ManualOverride,
                                            std::move(layer)};

        job->store->commitDerivedLayer(std::move(commitReq), job->request.context)
            .then([job](Result<CommitDerivedLayerResponse> commitResp) {
                if (!commitResp) {
                    job->promise.addResult(std::unexpected(commitResp.error()));
                    job->promise.finish();
                    return;
                }
                PreparedPageManualOverrideChange change{
                    job->request.projectId,
                    job->request.pageId,
                    job->beforeRef,
                    commitResp->relativePath,
                    job->request.expectedSessionGeneration,
                    job->request.expectedPageSize,
                };
                job->promise.addResult(change);
                job->promise.finish();
            });
    });

    return future;
}

} // namespace manga::application
