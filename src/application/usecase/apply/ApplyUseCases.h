#pragma once

#include "application/dto/ClearDto.h"
#include "application/dto/LayoutDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IDerivedAssetStore.h"
#include "domain/model/ComicProject.h"

#include <QColorSpace>
#include <QFuture>
#include <QPainter>
#include <QPoint>
#include <QSize>
#include <QtMath>

#include <expected>
#include <memory>
#include <utility>

namespace manga::application {
namespace detail {

[[nodiscard]] inline ApplicationError applyError(ErrorCategory category, QString code, QString message) {
    return makeApplicationError(category, std::move(code), std::move(message));
}

} // namespace detail

struct PrepareClearResultRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId expectedRegionId;
    QSize pageSize;      // normalized working image size, used for a fresh transparent layer
    QString projectRoot; // runtime absolute path, never persisted
    ClearResponse result;
    QVector<ClearResponse> results;
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    quint64 expectedRevision{0}; // region contentRevision captured when the clear was requested
    QHash<domain::RegionId, quint64> expectedRevisions;
    OperationContext context;
};
struct PreparedClearChange {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId regionId;
    QVector<domain::RegionId> regionIds;
    quint64 expectedRevision{0};
    QString beforeRef;
    QString afterRef;
    bool beforeState{false};
    bool afterState{true};
    QHash<domain::RegionId, bool> beforeStates;
};

/// Prepares clear patches by compositing them onto the current page clear layer, and committing
/// the result through the derived asset store. It does NOT update the Domain. Prepare may
/// produce an unreferenced immutable asset if the request becomes stale after preparation
/// starts; the subsequent Apply stage is authoritative for stale checks.
/// Any failure leaves the old layer reference and source image untouched.
class PrepareClearResultUseCase {
public:
    PrepareClearResultUseCase(domain::ComicProject* project, IDerivedAssetStore* store)
        : m_project(project), m_store(store) {}

    [[nodiscard]] QFuture<Result<PreparedClearChange>> execute(const PrepareClearResultRequest& request) {
        if (m_project == nullptr || m_store == nullptr) {
            return makeFailedFuture<PreparedClearChange>(detail::applyError(
                ErrorCategory::Internal, "internal.null_apply_dependency", "Apply dependency is unavailable"));
        }
        if (m_project->id() != request.projectId) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Project, "project.mismatch", "Project changed"));
        }
        if (request.sessionGeneration != request.currentSessionGeneration) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Cancelled, "cancelled.stale_generation", "Clear result is stale"));
        }

        QVector<ClearResponse> allResults = request.results;
        if (allResults.isEmpty() && request.expectedRegionId.isValid() && request.result.regionId.isValid()) {
            allResults.append(request.result);
        }
        if (allResults.isEmpty()) {
            return makeFailedFuture<PreparedClearChange>(detail::applyError(
                ErrorCategory::Protocol, "protocol.invalid_clear_result", "No clear results provided"));
        }

        const auto* page = m_project->findPage(request.pageId);
        if (page == nullptr) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
        }

        QHash<domain::RegionId, bool> beforeStates;
        QVector<domain::RegionId> validRegionIds;

        for (const auto& item : allResults) {
            if (!item.regionId.isValid()) {
                return makeFailedFuture<PreparedClearChange>(
                    detail::applyError(ErrorCategory::Protocol,
                                       "protocol.region_mismatch",
                                       "Clear response region does not match request"));
            }
            const auto* region = page->findRegion(item.regionId);
            if (region == nullptr) {
                return makeFailedFuture<PreparedClearChange>(
                    detail::applyError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
            }
            quint64 expectedRev = request.expectedRevisions.value(item.regionId, request.expectedRevision);
            if (region->contentRevision() != expectedRev) {
                return makeFailedFuture<PreparedClearChange>(
                    detail::applyError(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
            }
            if (region->isLocked()) {
                return makeFailedFuture<PreparedClearChange>(detail::applyError(
                    ErrorCategory::Input, "input.manual_override", "Region is under manual control"));
            }
            if (request.pageSize.isEmpty() || item.patch.isNull() || item.patch.format() != QImage::Format_RGBA8888 ||
                !item.affectedBounds.isValid()) {
                return makeFailedFuture<PreparedClearChange>(detail::applyError(
                    ErrorCategory::Protocol, "protocol.invalid_clear_result", "Clear result is invalid"));
            }

            const int left = qFloor(item.affectedBounds.x);
            const int top = qFloor(item.affectedBounds.y);
            const int width = qCeil(item.affectedBounds.x + item.affectedBounds.width) - left;
            const int height = qCeil(item.affectedBounds.y + item.affectedBounds.height) - top;
            if (width != item.patch.width() || height != item.patch.height() || left < 0 || top < 0 ||
                left + width > request.pageSize.width() || top + height > request.pageSize.height()) {
                return makeFailedFuture<PreparedClearChange>(
                    detail::applyError(ErrorCategory::Protocol,
                                       "protocol.invalid_clear_result",
                                       "Clear patch does not match the affected bounds"));
            }
            validRegionIds.append(item.regionId);
            beforeStates.insert(item.regionId, region->isCleared());
        }

        const QString relativePath =
            page->clearLayerAsset().has_value() ? QString::fromStdString(*page->clearLayerAsset()) : QString();

        auto promise = std::make_shared<QPromise<Result<PreparedClearChange>>>();
        promise->start();
        auto future = promise->future();

        auto* store = m_store;
        const auto requestSnapshot = request;
        QFuture<Result<QImage>> layerFuture;

        if (relativePath.isEmpty()) {
            QImage transparent(requestSnapshot.pageSize, QImage::Format_RGBA8888);
            transparent.fill(0);
            transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
            layerFuture = makeReadyFuture<QImage>(std::move(transparent));
        } else {
            ReadDerivedLayerRequest readRequest;
            readRequest.projectId = requestSnapshot.projectId;
            readRequest.projectRoot = requestSnapshot.projectRoot;
            readRequest.pageId = requestSnapshot.pageId;
            readRequest.layerKind = DerivedLayerKind::Clear;
            readRequest.relativePath = relativePath;
            layerFuture = store->readDerivedLayer(std::move(readRequest), requestSnapshot.context);
        }

        bool firstBeforeState = beforeStates.value(validRegionIds.first(), false);

        layerFuture.then([store,
                          request = requestSnapshot,
                          promise,
                          relativePath,
                          allResults,
                          validRegionIds,
                          beforeStates,
                          firstBeforeState](Result<QImage> current) {
            if (!current) {
                promise->addResult(std::unexpected(current.error()));
                promise->finish();
                return;
            }
            if ((*current).size() != request.pageSize) {
                promise->addResult(std::unexpected(detail::applyError(
                    ErrorCategory::Input, "clear.layer_read_failed", "The stored clear layer is not usable")));
                promise->finish();
                return;
            }
            // The PNG round-trip decodes to ARGB32_Premultiplied; normalize so the commit
            // stores the same straight-alpha format that export consumes.
            QImage baseLayer = *current;
            if (baseLayer.format() != QImage::Format_RGBA8888) {
                baseLayer = baseLayer.convertToFormat(QImage::Format_RGBA8888);
            }
            {
                QPainter painter(&baseLayer);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                for (const auto& item : allResults) {
                    const int left = qFloor(item.affectedBounds.x);
                    const int top = qFloor(item.affectedBounds.y);
                    painter.drawImage(QPoint(left, top), item.patch);
                }
            }
            baseLayer.setColorSpace(QColorSpace(QColorSpace::SRgb));

            CommitDerivedLayerRequest commitRequest;
            commitRequest.projectId = request.projectId;
            commitRequest.projectRoot = request.projectRoot;
            commitRequest.pageId = request.pageId;
            commitRequest.layerKind = DerivedLayerKind::Clear;
            commitRequest.layer = baseLayer;
            store->commitDerivedLayer(std::move(commitRequest), request.context)
                .then([request, promise, relativePath, validRegionIds, beforeStates, firstBeforeState](
                          Result<CommitDerivedLayerResponse> committed) {
                    if (!committed) {
                        promise->addResult(std::unexpected(committed.error()));
                        promise->finish();
                        return;
                    }
                    PreparedClearChange change{request.projectId,
                                               request.pageId,
                                               validRegionIds.first(),
                                               validRegionIds,
                                               request.expectedRevision,
                                               relativePath,
                                               committed->relativePath,
                                               firstBeforeState,
                                               true,
                                               beforeStates};
                    promise->addResult(change);
                    promise->finish();
                });
        });
        return future;
    }

private:
    domain::ComicProject* m_project;
    IDerivedAssetStore* m_store;
};

struct ApplyLayoutResultRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId expectedRegionId;
    LayoutResponse result;
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    quint64 expectedRevision{0};
};

struct ApplyPreparedClearChangeRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId expectedRegionId;
    QString targetRef;
    bool targetState{false};
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    quint64 expectedRevision{0};
};

/// Adopts a prepared clear change into the Domain synchronously.
class ApplyPreparedClearChangeUseCase {
public:
    explicit ApplyPreparedClearChangeUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<void> execute(const ApplyPreparedClearChangeRequest& request) {
        if (m_project == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
        }
        if (m_project->id() != request.projectId) {
            return std::unexpected(detail::applyError(ErrorCategory::Project, "project.mismatch", "Project changed"));
        }
        if (request.sessionGeneration != request.currentSessionGeneration) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Cancelled, "cancelled.stale_generation", "Clear result is stale"));
        }
        if (!request.expectedRegionId.isValid()) {
            return std::unexpected(detail::applyError(
                ErrorCategory::Protocol, "protocol.region_mismatch", "Clear response region is invalid"));
        }
        const auto* page = m_project->findPage(request.pageId);
        if (page == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
        }
        const auto* region = page->findRegion(request.expectedRegionId);
        if (region == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
        }
        if (region->contentRevision() != request.expectedRevision) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
        }
        if (region->isLocked()) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.manual_override", "Region is under manual control"));
        }
        if (request.targetRef.isEmpty() && request.targetState) {
            return std::unexpected(detail::applyError(ErrorCategory::Protocol,
                                                      "protocol.invalid_clear_result",
                                                      "Clear change has empty path but state is cleared"));
        }

        std::optional<std::string> targetRef = std::nullopt;
        if (!request.targetRef.isEmpty()) {
            targetRef = request.targetRef.toStdString();
        }

        const auto applied =
            m_project->applyClearChangeOnPage(request.pageId, request.expectedRegionId, targetRef, request.targetState);
        if (!applied) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.edit_rejected", "Clear result could not be applied"));
        }
        return {};
    }

private:
    domain::ComicProject* m_project;
};

/// Adopts a resolved layout through the atomic region edit path; contentRevision increments
/// inside applyPageRegionEdits and stale results never reach the Domain.
class ApplyLayoutResultUseCase {
public:
    explicit ApplyLayoutResultUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<void> execute(const ApplyLayoutResultRequest& request) {
        if (m_project == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
        }
        if (m_project->id() != request.projectId) {
            return std::unexpected(detail::applyError(ErrorCategory::Project, "project.mismatch", "Project changed"));
        }
        if (request.sessionGeneration != request.currentSessionGeneration) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Cancelled, "cancelled.stale_generation", "Layout result is stale"));
        }
        if (!request.expectedRegionId.isValid() || request.result.regionId != request.expectedRegionId) {
            return std::unexpected(detail::applyError(
                ErrorCategory::Protocol, "protocol.region_mismatch", "Layout response region does not match request"));
        }
        if (!request.result.resolvedLayout.isValid()) {
            return std::unexpected(detail::applyError(
                ErrorCategory::Protocol, "protocol.invalid_layout_result", "Layout result is invalid"));
        }

        const auto* page = m_project->findPage(request.pageId);
        if (page == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
        }
        const auto* region = page->findRegion(request.expectedRegionId);
        if (region == nullptr) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
        }
        if (region->contentRevision() != request.expectedRevision) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
        }
        if (region->isLocked()) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.manual_override", "Region is under manual control"));
        }

        const domain::PageRegionEdit edits[] = {
            domain::PageRegionEdit{.regionId = request.expectedRegionId, .layout = request.result.resolvedLayout}};
        const auto applied = m_project->applyPageRegionEdits(request.pageId, edits);
        if (!applied) {
            return std::unexpected(
                detail::applyError(ErrorCategory::Input, "input.edit_rejected", "Layout result could not be applied"));
        }
        return {};
    }

private:
    domain::ComicProject* m_project;
};

} // namespace manga::application
