#include "RegionUseCases.h"

namespace manga::application {
namespace {

[[nodiscard]] ApplicationError regionError(ErrorCategory category, QString code, QString message) {
    return makeApplicationError(category, std::move(code), std::move(message));
}

constexpr auto kUndoInvariantBroken = "internal.undo_invariant_broken";

[[nodiscard]] Result<const domain::ComicPage*> findPage(const domain::ComicProject* project,
                                                        const domain::PageId& pageId) {
    if (project == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
    }
    return page;
}

[[nodiscard]] Result<void> updateGeometry(domain::ComicProject* project,
                                          const domain::PageId& pageId,
                                          const domain::RegionId& regionId,
                                          const domain::RectGeometry& geometry) {
    const auto page = findPage(project, pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    if ((*page)->findRegion(regionId) == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
    }
    if (!geometry.isValid() || !geometry.isWithinBounds((*page)->imageSize())) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_geometry", "Region geometry is outside the page"));
    }
    const auto result = project->updateRegionGeometryOnPage(pageId, regionId, geometry);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.geometry_rejected", "Region geometry was rejected"));
    }
    return {};
}

} // namespace

Result<domain::RegionId> CreateRegionUseCase::execute(const CreateRegionRequest& request) {
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    if (!request.rect.isValid() || !request.rect.isWithinBounds((*page)->imageSize())) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_geometry", "Region geometry is outside the page"));
    }
    if (request.orderIndex != static_cast<int>((*page)->regions().size())) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_order", "New regions must be appended in reading order"));
    }

    const auto regionId = domain::RegionId::generate();
    domain::TextRegion region(regionId, request.rect);
    const auto result = m_project->addRegionToPage(request.pageId, std::move(region));
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.region_rejected", "Region could not be created"));
    }
    return regionId;
}

Result<void> DeleteRegionUseCase::execute(const DeleteRegionRequest& request) {
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    if ((*page)->findRegion(request.regionId) == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
    }
    const auto result = m_project->removeRegionFromPage(request.pageId, request.regionId);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.region_rejected", "Region could not be deleted"));
    }
    return {};
}

Result<void> MoveRegionUseCase::execute(const MoveRegionRequest& request) {
    return updateGeometry(m_project, request.pageId, request.regionId, request.newRect);
}

Result<void> ResizeRegionUseCase::execute(const ResizeRegionRequest& request) {
    return updateGeometry(m_project, request.pageId, request.regionId, request.newRect);
}

Result<void> ReorderRegionUseCase::execute(const ReorderRegionRequest& request) {
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    if ((*page)->findRegion(request.regionId) == nullptr || request.newOrderIndex < 0 ||
        request.newOrderIndex >= static_cast<int>((*page)->regions().size())) {
        return std::unexpected(regionError(ErrorCategory::Input, "input.invalid_order", "Region order is invalid"));
    }
    const auto result = m_project->reorderRegionOnPage(request.pageId, request.regionId, request.newOrderIndex);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_order", "Region order could not be changed"));
    }
    return {};
}

// Use case validation produces user-facing Input error messages. The domain layer remains the authoritative invariant
// source.
Result<void> InsertRegionUseCase::execute(InsertRegionRequest request) {
    if (m_project == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    if (!request.region.geometry().isValid() || !request.region.geometry().isWithinBounds((*page)->imageSize())) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_geometry", "Region geometry is outside the page"));
    }
    const int regionCount = static_cast<int>((*page)->regions().size());
    if (request.orderIndex < 0 || request.orderIndex > regionCount) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.invalid_order", "Insert order index is out of range"));
    }
    auto result = m_project->insertRegionOnPage(request.pageId, std::move(request.region), request.orderIndex);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.region_rejected", "Region could not be inserted"));
    }
    return {};
}

Result<RemovedRegion> RemoveRegionUseCase::execute(const RemoveRegionRequest& request) {
    if (m_project == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }
    const auto* existing = (*page)->findRegion(request.regionId);
    if (existing == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
    }
    RemovedRegion removed{.region = *existing, .orderIndex = existing->orderIndex()};
    auto result = m_project->removeRegionFromPage(request.pageId, request.regionId);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Input, "input.region_rejected", "Region could not be removed"));
    }
    return removed;
}

Result<void> ApplyRegionSnapshotsUseCase::execute(const ApplyRegionSnapshotsRequest& request) {
    if (m_project == nullptr) {
        return std::unexpected(regionError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    const auto page = findPage(m_project, request.pageId);
    if (!page) {
        return std::unexpected(page.error());
    }

    // Translation content is trusted from snapshots because snapshots only carry values that were already domain-valid.
    // This gate validates structure, geometry and layout.
    for (const auto& edit : request.edits) {
        const auto* region = (*page)->findRegion(edit.regionId);
        if (region == nullptr) {
            return std::unexpected(
                regionError(ErrorCategory::Internal, kUndoInvariantBroken, "Snapshot references a missing region"));
        }
        if (edit.geometry.has_value()) {
            if (!edit.geometry->isValid() || !edit.geometry->isWithinBounds((*page)->imageSize())) {
                return std::unexpected(
                    regionError(ErrorCategory::Internal, kUndoInvariantBroken, "Snapshot contains invalid geometry"));
            }
        }
        if (edit.layout.has_value() && !edit.layout->isValid()) {
            return std::unexpected(
                regionError(ErrorCategory::Internal, kUndoInvariantBroken, "Snapshot contains invalid layout"));
        }
    }

    if (request.edits.empty()) {
        return {};
    }

    auto result = m_project->applyPageRegionEdits(request.pageId, request.edits);
    if (!result) {
        return std::unexpected(
            regionError(ErrorCategory::Internal, kUndoInvariantBroken, "Snapshot batch edit was rejected"));
    }
    return {};
}

} // namespace manga::application
