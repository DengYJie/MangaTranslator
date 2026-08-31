#include "ui/command/EditorCommandContext.h"

namespace manga::ui::command {

EditorCommandContext::EditorCommandContext(domain::ComicProject* project) : m_project(project) {}

application::Result<void>
EditorCommandContext::insertRegion(const domain::PageId& pageId, const domain::TextRegion& region, int orderIndex) {
    application::InsertRegionUseCase useCase(m_project);
    application::InsertRegionRequest request;
    request.pageId = pageId;
    request.region = region;
    request.orderIndex = orderIndex;
    return useCase.execute(std::move(request));
}

application::Result<application::RemovedRegion> EditorCommandContext::removeRegion(const domain::PageId& pageId,
                                                                                   const domain::RegionId& regionId) {
    application::RemoveRegionUseCase useCase(m_project);
    application::RemoveRegionRequest request;
    request.pageId = pageId;
    request.regionId = regionId;
    return useCase.execute(request);
}

application::Result<void> EditorCommandContext::applySnapshots(const domain::PageId& pageId,
                                                               const std::vector<domain::PageRegionEdit>& edits) {
    application::ApplyRegionSnapshotsUseCase useCase(m_project);
    application::ApplyRegionSnapshotsRequest request;
    request.pageId = pageId;
    request.edits = edits;
    return useCase.execute(request);
}

std::optional<domain::RectGeometry> EditorCommandContext::regionGeometry(const domain::PageId& pageId,
                                                                         const domain::RegionId& regionId) const {
    if (m_project == nullptr) {
        return std::nullopt;
    }
    const auto* page = m_project->findPage(pageId);
    if (page == nullptr) {
        return std::nullopt;
    }
    const auto* region = page->findRegion(regionId);
    if (region == nullptr) {
        return std::nullopt;
    }
    return region->geometry();
}

domain::ImageSize EditorCommandContext::pageSize(const domain::PageId& pageId) const {
    if (m_project == nullptr) {
        return {};
    }
    const auto* page = m_project->findPage(pageId);
    if (page == nullptr) {
        return {};
    }
    return page->imageSize();
}

} // namespace manga::ui::command
