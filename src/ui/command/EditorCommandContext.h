#pragma once

#include "application/error/ApplicationError.h"
#include "application/usecase/region/RegionUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/Geometry.h"
#include "domain/model/TextRegion.h"
#include "domain/model/Uuid.h"

#include <QVector>

namespace manga::ui::command {

/// Non-owning bridge between QUndoCommands and the domain aggregate.
/// Commands call only through this context; they never touch ComicProject directly.
class EditorCommandContext {
public:
    explicit EditorCommandContext(domain::ComicProject* project);

    [[nodiscard]] domain::ComicProject* project() const noexcept {
        return m_project;
    }

    /// Inserts a region with a pre-generated ID at the given order position.
    [[nodiscard]] application::Result<void>
    insertRegion(const domain::PageId& pageId, const domain::TextRegion& region, int orderIndex);

    /// Removes a region and returns its full TextRegion plus original orderIndex for undo.
    [[nodiscard]] application::Result<application::RemovedRegion> removeRegion(const domain::PageId& pageId,
                                                                               const domain::RegionId& regionId);

    /// Applies a batch of region edits atomically via ApplyRegionSnapshotsUseCase.
    [[nodiscard]] application::Result<void> applySnapshots(const domain::PageId& pageId,
                                                           const std::vector<domain::PageRegionEdit>& edits);

    /// Returns the current geometry for a region on a page, or std::nullopt if not found.
    [[nodiscard]] std::optional<domain::RectGeometry> regionGeometry(const domain::PageId& pageId,
                                                                     const domain::RegionId& regionId) const;

    /// Page image bounds for geometry validation.
    [[nodiscard]] domain::ImageSize pageSize(const domain::PageId& pageId) const;

private:
    domain::ComicProject* m_project; // non-owning
};

} // namespace manga::ui::command
