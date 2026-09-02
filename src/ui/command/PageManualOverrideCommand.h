#pragma once

#include "application/dto/ManualPaintDto.h"
#include "ui/command/EditorCommandContext.h"

#include <QUndoCommand>

#include <functional>

namespace manga::ui::command {

/// Undoable adoption of one prepared page manual override layer (paint or erase).
/// This command only changes the page reference after validating expected preconditions.
class PageManualOverrideCommand final : public QUndoCommand {
public:
    using Invalidation = std::function<void(const domain::PageId&)>;

    PageManualOverrideCommand(EditorCommandContext* context,
                              application::PreparedPageManualOverrideChange prepared,
                              Invalidation invalidate = {},
                              QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    [[nodiscard]] bool hasFaulted() const noexcept {
        return m_faulted;
    }

private:
    bool applyRef(const QString& expected, const QString& target);

    EditorCommandContext* m_context{nullptr};
    application::PreparedPageManualOverrideChange m_prepared;
    Invalidation m_invalidate;
    bool m_firstRedo{true};
    bool m_faulted{false};
};

} // namespace manga::ui::command
