#pragma once

#include "application/error/ApplicationError.h"
#include "domain/model/ComicProject.h"

#include <vector>

namespace manga::application {

struct CreateRegionRequest {
    domain::PageId pageId;
    domain::RectGeometry rect;
    int orderIndex{0};
};

struct DeleteRegionRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
};

struct MoveRegionRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    domain::RectGeometry newRect;
};

struct ResizeRegionRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    domain::RectGeometry newRect;
};

class CreateRegionUseCase {
public:
    explicit CreateRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// Requires a rect whose sides reach at least RectGeometry::kMinDimension and which sits fully
    /// inside the page image, and an orderIndex equal to the page's current region count, so a
    /// creation always appends. Returns the generated region id. A rejected rect reports
    /// input.invalid_geometry and a non-append index reports input.invalid_order.
    [[nodiscard]] Result<domain::RegionId> execute(const CreateRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class DeleteRegionUseCase {
public:
    explicit DeleteRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// Requires the region to exist on the page and reports input.region_not_found when it does
    /// not. The removed region is discarded, so RemoveRegionUseCase is the undo capable partner.
    Result<void> execute(const DeleteRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class MoveRegionUseCase {
public:
    explicit MoveRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// Requires the region to exist on the page and newRect to satisfy the geometry rule stated on
    /// CreateRegionUseCase. A missing region reports input.region_not_found, a rect outside the
    /// page reports input.invalid_geometry, and a domain edit that refuses the value reports
    /// input.geometry_rejected.
    Result<void> execute(const MoveRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class ResizeRegionUseCase {
public:
    explicit ResizeRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// Shares the preconditions, postconditions, and error behavior of MoveRegionUseCase because
    /// both replace the geometry of an existing region and neither changes its order index.
    Result<void> execute(const ResizeRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

struct ReorderRegionRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    int newOrderIndex{0};
};

class ReorderRegionUseCase {
public:
    explicit ReorderRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// Requires the region to exist on the page and a newOrderIndex below its current region
    /// count, counted from zero. The page moves the region and renumbers the others so indices stay
    /// contiguous, and a missing region or an out of range index reports input.invalid_order.
    Result<void> execute(const ReorderRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

struct InsertRegionRequest {
    domain::PageId pageId;
    domain::TextRegion region;
    int orderIndex{0};
};

/// Inserts a region at an exact order position, which lets undo reinsert a known RegionId. The
/// orderIndex may reach the current region count so an append stays expressible, and an index
/// outside that range reports input.invalid_order. This validation produces the user-facing Input
/// messages, while the domain page edit remains the authoritative gate for duplicate identity and
/// geometry.
class InsertRegionUseCase {
public:
    explicit InsertRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<void> execute(InsertRegionRequest request);

private:
    domain::ComicProject* m_project;
};

struct RemoveRegionRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
};

struct RemovedRegion {
    domain::TextRegion region;
    int orderIndex{0};
};

/// Removes a region and returns the full TextRegion plus its original orderIndex for undo.
class RemoveRegionUseCase {
public:
    explicit RemoveRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<RemovedRegion> execute(const RemoveRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

struct ApplyRegionSnapshotsRequest {
    domain::PageId pageId;
    std::vector<domain::PageRegionEdit> edits;
};

/// Validates all edits for the page before applying any. Any failure modifies nothing.
class ApplyRegionSnapshotsUseCase {
public:
    explicit ApplyRegionSnapshotsUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<void> execute(const ApplyRegionSnapshotsRequest& request);

private:
    domain::ComicProject* m_project;
};

} // namespace manga::application
