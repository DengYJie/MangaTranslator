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
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    quint64 expectedRevision{0}; // region contentRevision captured when the clear was requested
    OperationContext context;
};
struct PreparedClearChange {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId regionId;
    quint64 expectedRevision{0};
    QString beforeRef;
    QString afterRef;
    bool beforeState{false};
    bool afterState{true};
};

/// Prepares a clear patch by compositing it onto the current page clear layer, and committing
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
        if (!request.expectedRegionId.isValid() || request.result.regionId != request.expectedRegionId) {
            return makeFailedFuture<PreparedClearChange>(detail::applyError(
                ErrorCategory::Protocol, "protocol.region_mismatch", "Clear response region does not match request"));
        }
        const auto* page = m_project->findPage(request.pageId);
        if (page == nullptr) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
        }
        const auto* region = page->findRegion(request.expectedRegionId);
        if (region == nullptr) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
        }
        if (region->contentRevision() != request.expectedRevision) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
        }
        if (region->isLocked()) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Input, "input.manual_override", "Region is under manual control"));
        }
        if (request.pageSize.isEmpty() || request.result.patch.isNull() ||
            request.result.patch.format() != QImage::Format_RGBA8888 || !request.result.affectedBounds.isValid()) {
            return makeFailedFuture<PreparedClearChange>(detail::applyError(
                ErrorCategory::Protocol, "protocol.invalid_clear_result", "Clear result is invalid"));
        }

        const int left = qFloor(request.result.affectedBounds.x);
        const int top = qFloor(request.result.affectedBounds.y);
        const int width = qCeil(request.result.affectedBounds.x + request.result.affectedBounds.width) - left;
        const int height = qCeil(request.result.affectedBounds.y + request.result.affectedBounds.height) - top;
        if (width != request.result.patch.width() || height != request.result.patch.height() || left < 0 || top < 0 ||
            left + width > request.pageSize.width() || top + height > request.pageSize.height()) {
            return makeFailedFuture<PreparedClearChange>(
                detail::applyError(ErrorCategory::Protocol,
                                   "protocol.invalid_clear_result",
                                   "Clear patch does not match the affected bounds"));
        }

        const QString relativePath =
            page->clearLayerAsset().has_value() ? QString::fromStdString(*page->clearLayerAsset()) : QString();

        // Manual promise chaining: Qt does not unwrap nested QFuture returns in then(), and
        // each failure path must finish the promise exactly once.
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

        bool beforeState = region->isCleared();

        layerFuture.then(
            [store, request = requestSnapshot, promise, relativePath, beforeState](Result<QImage> current) {
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
                const int left = qFloor(request.result.affectedBounds.x);
                const int top = qFloor(request.result.affectedBounds.y);
                QPainter painter(&baseLayer);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawImage(QPoint(left, top), request.result.patch);
                painter.end();
                baseLayer.setColorSpace(QColorSpace(QColorSpace::SRgb));

                CommitDerivedLayerRequest commitRequest;
                commitRequest.projectId = request.projectId;
                commitRequest.projectRoot = request.projectRoot;
                commitRequest.pageId = request.pageId;
                commitRequest.layerKind = DerivedLayerKind::Clear;
                commitRequest.layer = baseLayer;
                store->commitDerivedLayer(std::move(commitRequest), request.context)
                    .then([request, promise, relativePath, beforeState](Result<CommitDerivedLayerResponse> committed) {
                        if (!committed) {
                            promise->addResult(std::unexpected(committed.error()));
                            promise->finish();
                            return;
                        }
                        PreparedClearChange change{request.projectId,
                                                   request.pageId,
                                                   request.expectedRegionId,
                                                   request.expectedRevision,
                                                   relativePath,
                                                   committed->relativePath,
                                                   beforeState,
                                                   true};
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
