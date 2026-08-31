#pragma once

#include "ui/command/EditorCommandContext.h"
#include "ui/manga/RegionInteraction.h"

#include <QUndoCommand>
#include <QVector>

namespace manga::ui::command {

/// Creates a single region on first redo; removes it on undo.
/// The RegionId is generated before construction so undo/redo keeps the same identity.
class CreateRegionCommand : public QUndoCommand {
public:
    CreateRegionCommand(EditorCommandContext* context,
                        domain::PageId pageId,
                        domain::RegionId regionId,
                        domain::RectGeometry geometry,
                        int orderIndex);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept {
        return m_pageId;
    }
    [[nodiscard]] domain::RegionId regionId() const noexcept {
        return m_regionId;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    domain::RegionId m_regionId;
    domain::RectGeometry m_geometry;
    int m_orderIndex{0};
    bool m_firstRedo{true};
};

/// Deletes a batch of regions on first redo; reinserts them on undo.
/// Snapshots (full TextRegion + orderIndex) are captured at construction time sorted by order.
class DeleteRegionsCommand : public QUndoCommand {
public:
    /// Each entry pairs a full TextRegion snapshot with its original orderIndex.
    struct RegionEntry {
        domain::TextRegion region;
        int orderIndex{0};
    };

    DeleteRegionsCommand(EditorCommandContext* context, domain::PageId pageId, QVector<RegionEntry> entries);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept {
        return m_pageId;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    QVector<RegionEntry> m_entries; // sorted by orderIndex ascending
    bool m_firstRedo{true};
};

/// Stores before/after geometry for one region; applies via context applySnapshots.
class RegionGeometryCommand : public QUndoCommand {
public:
    RegionGeometryCommand(EditorCommandContext* context,
                          domain::PageId pageId,
                          domain::RegionId regionId,
                          domain::RectGeometry before,
                          domain::RectGeometry after,
                          canvas::EditKind kind);

    void undo() override;
    void redo() override;

    [[nodiscard]] int id() const override;
    [[nodiscard]] domain::PageId pageId() const noexcept {
        return m_pageId;
    }
    [[nodiscard]] domain::RegionId regionId() const noexcept {
        return m_regionId;
    }

    /// Returns true if the last redo or undo reported an internal failure.
    [[nodiscard]] bool hasFaulted() const noexcept {
        return m_faulted;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    domain::RegionId m_regionId;
    domain::RectGeometry m_before;
    domain::RectGeometry m_after;
    canvas::EditKind m_kind;
    bool m_faulted{false};
};

} // namespace manga::ui::command
