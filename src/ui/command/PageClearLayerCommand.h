#pragma once

#include "application/usecase/processing/ManualInpaintWorkflow.h"
#include "ui/command/EditorCommandContext.h"

#include <QUndoCommand>

#include <functional>

namespace manga::ui::command {

/// Undoable adoption of one prepared page clear layer. Asset preparation is deliberately
/// separate: this command only changes the page reference after validating the expected ref.
class PageClearLayerCommand final : public QUndoCommand {
public:
    using Invalidation = std::function<void(const domain::PageId&)>;

    PageClearLayerCommand(EditorCommandContext* context,
                          application::PreparedPageInpaintChange prepared,
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
    application::PreparedPageInpaintChange m_prepared;
    Invalidation m_invalidate;
    bool m_firstRedo{true};
    bool m_faulted{false};
};

} // namespace manga::ui::command
