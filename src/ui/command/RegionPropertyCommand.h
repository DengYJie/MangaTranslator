#pragma once

#include "ui/command/RegionCommands.h"

#include <QVector>

namespace manga::ui::command {

/// Property slots editable through the Inspector. Coarser than the panel-level
/// InspectorField: the four margin sides share Margins and both alignment axes
/// share Alignment, because one command payload covers the whole layout value.
enum class RegionPropertyField {
    SourceText,
    ManualTranslation,
    Candidate,
    FontFamily,
    FontWeight,
    LetterSpacing,
    FontSize,
    Orientation,
    Alignment,
    LineSpacing,
    Margins,
    TextColor,
    StrokeColor,
    StrokeWidth
};

/// Merge identity of a text edit batch. Commands merge only when every member
/// matches; sessionId changes whenever the batch is sealed.
struct TextEditSessionKey {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId regionId;
    RegionPropertyField field{RegionPropertyField::SourceText};
    quint64 sessionId{0};

    bool operator==(const TextEditSessionKey&) const = default;
};

/// Undoable edit of region content or layout properties.
///
/// Two shapes share one implementation:
/// - a merge-eligible single-region text edit carrying a TextEditSessionKey and a
///   monotonic timestamp; consecutive commands merge when the key matches and the
///   monotonic interval stays within kMergeWindowMs;
/// - a non-mergeable macro command holding per-region before/after PageRegionEdit
///   values (batch style edits and candidate adoption).
class RegionPropertyCommand : public RegionCommand {
public:
    struct PropertyEdit {
        domain::RegionId regionId;
        domain::PageRegionEdit before;
        domain::PageRegionEdit after;
    };

    static constexpr qint64 kMergeWindowMs{500};

    /// Merge-eligible text edit inside an active TextEditSession.
    RegionPropertyCommand(EditorCommandContext* context,
                          TextEditSessionKey key,
                          qint64 timestampMs,
                          domain::PageRegionEdit before,
                          domain::PageRegionEdit after,
                          const QString& text);

    /// Non-mergeable macro command over per-region before/after edits.
    RegionPropertyCommand(EditorCommandContext* context,
                          domain::PageId pageId,
                          QVector<PropertyEdit> edits,
                          const QString& text);

    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;
    void undo() override;
    void redo() override;

    [[nodiscard]] domain::PageId pageId() const noexcept override {
        return m_pageId;
    }
    [[nodiscard]] const QVector<PropertyEdit>& edits() const noexcept {
        return m_edits;
    }

private:
    void apply(bool useAfter);

    EditorCommandContext* m_context; // non-owning
    domain::PageId m_pageId;
    QVector<PropertyEdit> m_edits;
    /// Set only for merge-eligible text edits; empty for macro commands.
    std::optional<TextEditSessionKey> m_mergeKey;
    qint64 m_timestampMs{0}; // monotonic milliseconds, never wall clock
};

} // namespace manga::ui::command
