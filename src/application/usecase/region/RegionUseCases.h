#pragma once

#include "application/error/ApplicationError.h"
#include "domain/model/ComicProject.h"

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

    /// rect >= 2x2, strictly within page image bounds. orderIndex contiguous 0..N-1.
    [[nodiscard]] Result<domain::RegionId> execute(const CreateRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class DeleteRegionUseCase {
public:
    explicit DeleteRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// region must exist on the page.
    Result<void> execute(const DeleteRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class MoveRegionUseCase {
public:
    explicit MoveRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// region must exist, new rect must be within page bounds (0 <= index < N).
    Result<void> execute(const MoveRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

class ResizeRegionUseCase {
public:
    explicit ResizeRegionUseCase(domain::ComicProject* project) : m_project(project) {}

    /// new rect must be valid and within page image bounds.
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

    /// new orderIndex must be valid (0 <= index < N).
    Result<void> execute(const ReorderRegionRequest& request);

private:
    domain::ComicProject* m_project;
};

} // namespace manga::application
