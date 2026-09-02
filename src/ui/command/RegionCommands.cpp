#include "ui/command/RegionCommands.h"

namespace manga::ui::command {

// CreateTextRegionCommand

CreateTextRegionCommand::CreateTextRegionCommand(EditorCommandContext* context,
                                                 domain::PageId pageId,
                                                 domain::RegionId regionId,
                                                 domain::RectGeometry geometry,
                                                 domain::TextLayout layout,
                                                 int orderIndex)
    : RegionCommand(QObject::tr("新建文字框")), m_context(context), m_pageId(pageId), m_regionId(regionId),
      m_geometry(geometry), m_layout(std::move(layout)), m_orderIndex(orderIndex) {}

void CreateTextRegionCommand::undo() {
    const auto result = m_context->removeRegion(m_pageId, m_regionId);
    if (!result.has_value()) {
        markFaulted();
    }
}

void CreateTextRegionCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
    } else {
        // Reinsertion appends at the end of the region list. No command reorders regions, so the
        // recreated region keeps the contiguous order the domain requires.
        const auto* page = m_context->project()->findPage(m_pageId);
        if (page != nullptr) {
            m_orderIndex = static_cast<int>(page->regions().size());
        }
    }
    domain::TextRegion region(m_regionId, m_geometry, m_orderIndex);
    region.setLayout(m_layout);
    const auto result = m_context->insertRegion(m_pageId, region, m_orderIndex);
    if (!result.has_value()) {
        markFaulted();
    }
}

// CreateRegionsBatchCommand

CreateRegionsBatchCommand::CreateRegionsBatchCommand(EditorCommandContext* context,
                                                     domain::PageId pageId,
                                                     QVector<Entry> entries)
    : RegionCommand(QObject::tr("自动检测文字框")), m_context(context), m_pageId(pageId),
      m_entries(std::move(entries)) {}

void CreateRegionsBatchCommand::undo() {
    // Remove in reverse order
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const auto result = m_context->removeRegion(m_pageId, m_entries[i].regionId);
        if (!result.has_value()) {
            markFaulted();
        }
    }
}

void CreateRegionsBatchCommand::redo() {
    if (m_firstRedo) {
        m_firstRedo = false;
    }
    for (const auto& entry : std::as_const(m_entries)) {
        domain::TextRegion region(
            entry.regionId, entry.geometry, entry.bubbleGeometry, entry.orderIndex, entry.bubblePolygon);
        const auto result = m_context->insertRegion(m_pageId, region, entry.orderIndex);
        if (!result.has_value()) {
            markFaulted();
        }
    }
}

// DeleteRegionsCommand

DeleteRegionsCommand::DeleteRegionsCommand(EditorCommandContext* context,
                                           domain::PageId pageId,
                                           QVector<RegionEntry> entries)
    : RegionCommand(QObject::tr("删除文字框")), m_context(context), m_pageId(pageId), m_entries(std::move(entries)) {}

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
    : RegionCommand(kind == canvas::EditKind::Move ? QObject::tr("移动文字框") : QObject::tr("调整文字框大小")),
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

RegionGeometryBatchCommand::RegionGeometryBatchCommand(EditorCommandContext* context,
                                                       domain::PageId pageId,
                                                       QVector<Entry> entries,
                                                       canvas::EditKind kind)
    : RegionCommand(kind == canvas::EditKind::Move ? QObject::tr("移动文字框") : QObject::tr("调整文字框大小")),
      m_context(context), m_pageId(pageId), m_entries(std::move(entries)), m_kind(kind) {}

void RegionGeometryBatchCommand::undo() {
    apply(false);
}

void RegionGeometryBatchCommand::redo() {
    apply(true);
}

void RegionGeometryBatchCommand::apply(bool redo) {
    std::vector<domain::PageRegionEdit> edits;
    edits.reserve(static_cast<std::size_t>(m_entries.size()));
    for (const auto& entry : std::as_const(m_entries)) {
        domain::PageRegionEdit edit;
        edit.regionId = entry.regionId;
        edit.geometry = redo ? entry.after : entry.before;
        edits.push_back(edit);
    }
    const auto result = m_context->applySnapshots(m_pageId, edits);
    if (!result.has_value()) {
        markFaulted();
    }
}

} // namespace manga::ui::command
