#include "ProcessingAdoptionCommands.h"

#include "application/error/ApplicationError.h"
#include "domain/model/ComicProject.h"

namespace manga::ui::command {

ProcessingAdoptionCommand::ProcessingAdoptionCommand(EditorCommandContext* context,
                                                     application::PreparedProcessingAdoption prepared,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_context(context), m_prepared(std::move(prepared)) {
    setText(QObject::tr("应用处理结果"));
}

void ProcessingAdoptionCommand::undo() {
    if (m_faulted)
        return;

    for (const auto& change : m_prepared.changes) {
        domain::PageRegionEdit edit = change.before;
        const domain::PageRegionEdit edits[] = {edit};
        if (!m_context->project()->applyPageRegionEdits(change.pageId, edits)) {
            m_faulted = true;
            return;
        }
    }

    if (m_prepared.clearAssetChange) {
        const auto& c = *m_prepared.clearAssetChange;
        const auto regionList = c.regionIds.isEmpty() ? QVector<domain::RegionId>{c.regionId} : c.regionIds;
        for (const auto& rId : regionList) {
            const bool state = c.beforeStates.value(rId, c.beforeState);
            if (!m_context->project()->applyClearChangeOnPage(c.pageId,
                                                              rId,
                                                              c.beforeRef.isEmpty() ? std::optional<std::string>{}
                                                                                    : c.beforeRef.toStdString(),
                                                              state)) {
                m_faulted = true;
                return;
            }
        }
    }
}

void ProcessingAdoptionCommand::redo() {
    if (m_faulted)
        return;

    if (m_firstRedo) {
        m_firstRedo = false;
    }

    for (const auto& change : m_prepared.changes) {
        domain::PageRegionEdit edit = change.after;
        const domain::PageRegionEdit edits[] = {edit};
        if (!m_context->project()->applyPageRegionEdits(change.pageId, edits)) {
            m_faulted = true;
            return;
        }
    }

    if (m_prepared.clearAssetChange) {
        const auto& c = *m_prepared.clearAssetChange;
        const auto regionList = c.regionIds.isEmpty() ? QVector<domain::RegionId>{c.regionId} : c.regionIds;
        for (const auto& rId : regionList) {
            if (!m_context->project()->applyClearChangeOnPage(c.pageId,
                                                              rId,
                                                              c.afterRef.isEmpty() ? std::optional<std::string>{}
                                                                                   : c.afterRef.toStdString(),
                                                              c.afterState)) {
                m_faulted = true;
                return;
            }
        }
    }
}

} // namespace manga::ui::command
