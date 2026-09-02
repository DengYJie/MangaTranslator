#include "ManualPaintWorkflow.h"

#include "application/operation/AsyncResult.h"

#include <QColorSpace>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <memory>

namespace manga::application {
namespace {
ApplicationError makeError(ErrorCategory category, const char* code, const char* message) {
    return makeApplicationError(category, QString::fromUtf8(code), QString::fromUtf8(message));
}
} // namespace

void ManualPaintWorkflow::rasterizeStroke(
    QImage& image, ManualStrokeMode mode, const QColor& color, double diameter, const QVector<QPointF>& points) {
    if (points.isEmpty() || diameter <= 0.0 || image.isNull()) {
        return;
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    const double radius = diameter * 0.5;
    const int imgWidth = image.width();
    const int imgHeight = image.height();

    const uchar colorR = static_cast<uchar>(color.red());
    const uchar colorG = static_cast<uchar>(color.green());
    const uchar colorB = static_cast<uchar>(color.blue());
    const uchar colorA = static_cast<uchar>(color.alpha());

    // Iterate through consecutive point pairs, plus the terminal point
    struct Segment {
        QPointF start;
        QPointF end;
    };
    QVector<Segment> segments;
    segments.reserve(points.size());
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        segments.append({points[i], points[i + 1]});
    }
    if (!points.isEmpty()) {
        segments.append({points.last(), points.last()});
    }

    for (const auto& seg : segments) {
        const double startX = seg.start.x();
        const double startY = seg.start.y();
        const double endX = seg.end.x();
        const double endY = seg.end.y();

        const int left = std::max(0, static_cast<int>(std::floor(std::min(startX, endX) - radius - 0.5)));
        const int top = std::max(0, static_cast<int>(std::floor(std::min(startY, endY) - radius - 0.5)));
        const int right = std::min(imgWidth, static_cast<int>(std::ceil(std::max(startX, endX) + radius + 0.5)));
        const int bottom = std::min(imgHeight, static_cast<int>(std::ceil(std::max(startY, endY) + radius + 0.5)));

        const double dx = endX - startX;
        const double dy = endY - startY;
        const double lengthSquared = dx * dx + dy * dy;

        for (int y = top; y < bottom; ++y) {
            uchar* line = image.scanLine(y);
            const double py = static_cast<double>(y) + 0.5;
            for (int x = left; x < right; ++x) {
                const double px = static_cast<double>(x) + 0.5;
                double t = 0.0;
                if (lengthSquared > 0.0) {
                    t = std::clamp(((px - startX) * dx + (py - startY) * dy) / lengthSquared, 0.0, 1.0);
                }
                const double projX = startX + t * dx;
                const double projY = startY + t * dy;
                const double distSq = (px - projX) * (px - projX) + (py - projY) * (py - projY);
                const double distance = std::sqrt(distSq);

                const float coverage = static_cast<float>(std::clamp(radius + 0.5 - distance, 0.0, 1.0));
                if (coverage <= 0.0f) {
                    continue;
                }

                uchar* pixel = line + (x * 4);
                if (mode == ManualStrokeMode::Paint) {
                    const float srcA = (static_cast<float>(colorA) / 255.0f) * coverage;
                    const float dstA = static_cast<float>(pixel[3]) / 255.0f;
                    const float outA = srcA + dstA * (1.0f - srcA);
                    if (outA > 0.0f) {
                        const float srcR = static_cast<float>(colorR) / 255.0f;
                        const float srcG = static_cast<float>(colorG) / 255.0f;
                        const float srcB = static_cast<float>(colorB) / 255.0f;

                        const float dstR = static_cast<float>(pixel[0]) / 255.0f;
                        const float dstG = static_cast<float>(pixel[1]) / 255.0f;
                        const float dstB = static_cast<float>(pixel[2]) / 255.0f;

                        pixel[0] = static_cast<uchar>(std::clamp(
                            std::round(((srcR * srcA + dstR * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                        pixel[1] = static_cast<uchar>(std::clamp(
                            std::round(((srcG * srcA + dstG * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                        pixel[2] = static_cast<uchar>(std::clamp(
                            std::round(((srcB * srcA + dstB * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                    }
                    pixel[3] = static_cast<uchar>(std::clamp(std::round(outA * 255.0f), 0.0f, 255.0f));
                } else if (mode == ManualStrokeMode::Erase) {
                    const float dstA = static_cast<float>(pixel[3]);
                    pixel[3] = static_cast<uchar>(std::clamp(std::round(dstA * (1.0f - coverage)), 0.0f, 255.0f));
                }
            }
        }
    }
}

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

        // Execute deterministic distance field rasterization
        rasterizeStroke(layer, job->request.mode, job->request.color, job->request.diameter, job->request.points);

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
