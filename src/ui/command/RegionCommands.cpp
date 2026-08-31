#include "ui/command/RegionCommands.h"

namespace manga::ui::command {

// CreateRegionCommand

CreateRegionCommand::CreateRegionCommand(EditorCommandContext* context,
                                         domain::PageId pageId,
                                         domain::RegionId regionId,
                                         domain::RectGeometry geometry,
                                         int orderIndex)
    : RegionCommand(QStringLiteral("Create Region")), m_context(context), m_pageId(pageId), m_regionId(regionId),
      m_geometry(geometry), m_orderIndex(orderIndex) {}

void CreateRegionCommand::undo() {
    const auto result = m_context->removeRegion(m_pageId, m_regionId);
    if (!result.has_value()) {
        markFaulted();
    }
}

void CreateRegionCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
    } else {
        // Reinsertion appends at end; V0.1 has no command that reorders pages, so the
        // recreated region keeps a valid contiguous order.
        const auto* page = m_context->project()->findPage(m_pageId);
        if (page != nullptr) {
            m_orderIndex = static_cast<int>(page->regions().size());
        }
    }
    domain::TextRegion region(m_regionId, m_geometry, m_orderIndex);
    const auto result = m_context->insertRegion(m_pageId, region, m_orderIndex);
    if (!result.has_value()) {
        markFaulted();
    }
}

// DeleteRegionsCommand

DeleteRegionsCommand::DeleteRegionsCommand(EditorCommandContext* context,
                                           domain::PageId pageId,
                                           QVector<RegionEntry> entries)
    : RegionCommand(QStringLiteral("Delete Regions")), m_context(context), m_pageId(pageId),
      m_entries(std::move(entries)) {}

void DeleteRegionsCommand::undo() {
    // Reinsert in original order (ascending orderIndex).
    for (const auto& entry : std::as_const(m_entries)) {
        const auto result = m_context->insertRegion(m_pageId, entry.region, entry.orderIndex);
        if (!result.has_value()) {
            markFaulted();
        }
    }
}

void DeleteRegionsCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
    }
    // Remove in reverse order to avoid index shifting issues.
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const auto result = m_context->removeRegion(m_pageId, m_entries[i].region.id());
        if (!result.has_value()) {
            markFaulted();
        }
    }
}

// RegionGeometryCommand

RegionGeometryCommand::RegionGeometryCommand(EditorCommandContext* context,
                                             domain::PageId pageId,
                                             domain::RegionId regionId,
                                             domain::RectGeometry before,
                                             domain::RectGeometry after,
                                             canvas::EditKind kind)
    : RegionCommand(kind == canvas::EditKind::Move ? QStringLiteral("Move Region") : QStringLiteral("Resize Region")),
      m_context(context), m_pageId(pageId), m_regionId(regionId), m_before(before), m_after(after), m_kind(kind) {}

void RegionGeometryCommand::undo() {
    std::vector<domain::PageRegionEdit> edits;
    domain::PageRegionEdit edit;
    edit.regionId = m_regionId;
    edit.geometry = m_before;
    edits.push_back(edit);
    const auto result = m_context->applySnapshots(m_pageId, edits);
    if (!result.has_value()) {
        markFaulted();
    }
}

void RegionGeometryCommand::redo() {
    std::vector<domain::PageRegionEdit> edits;
    domain::PageRegionEdit edit;
    edit.regionId = m_regionId;
    edit.geometry = m_after;
    edits.push_back(edit);
    const auto result = m_context->applySnapshots(m_pageId, edits);
    if (!result.has_value()) {
        markFaulted();
    }
}

} // namespace manga::ui::command
