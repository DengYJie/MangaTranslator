#pragma once

#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "ui/command/EditorCommandContext.h"

#include <QUndoCommand>

namespace manga::ui::command {

class ProcessingAdoptionCommand : public QUndoCommand {
public:
    ProcessingAdoptionCommand(EditorCommandContext* context,
                              application::PreparedProcessingAdoption prepared,
                              QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    [[nodiscard]] bool hasFaulted() const noexcept {
        return m_faulted;
    }

    [[nodiscard]] domain::PageId pageId() const noexcept {
        if (!m_prepared.changes.isEmpty()) {
            return m_prepared.changes.first().pageId;
        }
        if (m_prepared.clearAssetChange) {
            return m_prepared.clearAssetChange->pageId;
        }
        return {};
    }

private:
    EditorCommandContext* m_context;
    application::PreparedProcessingAdoption m_prepared;
    bool m_firstRedo{true};
    bool m_faulted{false};
};

} // namespace manga::ui::command
