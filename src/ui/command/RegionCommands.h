#pragma once

#include "ui/command/EditorCommandContext.h"
#include "ui/manga/RegionInteraction.h"

#include <QUndoCommand>
#include <QVector>

namespace manga::ui::command {

/// Shared base for region history commands. Commands own a stable page target for
/// undo/redo navigation and a sticky fault flag set when a domain replay fails.
class RegionCommand : public QUndoCommand {
public:
    ~RegionCommand() override = default;

    [[nodiscard]] virtual domain::PageId pageId() const noexcept = 0;
    [[nodiscard]] bool hasFaulted() const noexcept {
        return m_faulted;
    }

protected:
    using QUndoCommand::QUndoCommand;

    void markFaulted() {
        m_faulted = true;
    }

private:
    bool m_faulted{false};
};

/// Creates a single region on first redo; removes it on undo.
/// The RegionId is generated before construction so undo/redo keeps the same identity.
class CreateRegionCommand : public RegionCommand {
public:
    CreateRegionCommand(EditorCommandContext* context,
                        domain::PageId pageId,
                        domain::RegionId regionId,
                        domain::RectGeometry geometry,
                        int orderIndex);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
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

/// Creates a batch of regions (e.g. from automatic layout detection) on first redo; removes them on undo.
class CreateRegionsBatchCommand : public RegionCommand {
public:
    struct Entry {
        domain::RegionId regionId;
        domain::RectGeometry geometry;
        std::optional<domain::RectGeometry> bubbleGeometry;
        int orderIndex{0};
    };

    CreateRegionsBatchCommand(EditorCommandContext* context, domain::PageId pageId, QVector<Entry> entries);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
        return m_pageId;
    }
    [[nodiscard]] const QVector<Entry>& entries() const noexcept {
        return m_entries;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    QVector<Entry> m_entries;
    bool m_firstRedo{true};
};

/// Deletes a batch of regions on first redo; reinserts them on undo.
/// Snapshots (full TextRegion + orderIndex) are captured at construction time sorted by order.
class DeleteRegionsCommand : public RegionCommand {
public:
    /// Each entry pairs a full TextRegion snapshot with its original orderIndex.
    struct RegionEntry {
        domain::TextRegion region;
        int orderIndex{0};
    };

    DeleteRegionsCommand(EditorCommandContext* context, domain::PageId pageId, QVector<RegionEntry> entries);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
        return m_pageId;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    QVector<RegionEntry> m_entries; // sorted by orderIndex ascending
    bool m_firstRedo{true};
};

/// Stores before/after geometry for one region; applies via context applySnapshots.
class RegionGeometryCommand : public RegionCommand {
public:
    RegionGeometryCommand(EditorCommandContext* context,
                          domain::PageId pageId,
                          domain::RegionId regionId,
                          domain::RectGeometry before,
                          domain::RectGeometry after,
                          canvas::EditKind kind);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
        return m_pageId;
    }
    [[nodiscard]] domain::RegionId regionId() const noexcept {
        return m_regionId;
    }

private:
    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    domain::RegionId m_regionId;
    domain::RectGeometry m_before;
    domain::RectGeometry m_after;
    canvas::EditKind m_kind;
};

class RegionGeometryBatchCommand : public RegionCommand {
public:
    struct Entry {
        domain::RegionId regionId;
        domain::RectGeometry before;
        domain::RectGeometry after;
    };

    RegionGeometryBatchCommand(EditorCommandContext* context,
                               domain::PageId pageId,
                               QVector<Entry> entries,
                               canvas::EditKind kind);

    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
        return m_pageId;
    }

private:
    void apply(bool redo);

    EditorCommandContext* m_context;
    domain::PageId m_pageId;
    QVector<Entry> m_entries;
    canvas::EditKind m_kind;
};

} // namespace manga::ui::command
