#include "PageManualOverrideCommand.h"

#include <utility>

namespace manga::ui::command {

PageManualOverrideCommand::PageManualOverrideCommand(EditorCommandContext* context,
                                                     application::PreparedPageManualOverrideChange prepared,
                                                     Invalidation invalidate,
                                                     QUndoCommand* parent)
    : QUndoCommand(parent), m_context(context), m_prepared(std::move(prepared)), m_invalidate(std::move(invalidate)) {
    setText(QObject::tr("涂抹 / 擦除"));
}

bool PageManualOverrideCommand::applyRef(const QString& expected, const QString& target) {
    if (m_context == nullptr || m_context->project() == nullptr || !m_prepared.projectId.isValid() ||
        m_context->project()->id() != m_prepared.projectId || !m_prepared.pageId.isValid()) {
        return false;
    }
    const auto* pageConst = m_context->project()->findPage(m_prepared.pageId);
    if (pageConst == nullptr || !m_prepared.expectedPageSize.isValid() ||
        pageConst->imageSize().width != m_prepared.expectedPageSize.width() ||
        pageConst->imageSize().height != m_prepared.expectedPageSize.height()) {
        return false;
    }
    const QString current = pageConst->manualOverrideLayerAsset().has_value()
                                ? QString::fromStdString(*pageConst->manualOverrideLayerAsset())
                                : QString();
    if (current != expected) {
        return false;
    }

    const auto mutation = m_context->project()->setManualOverrideLayerAssetOnPage(
        m_prepared.pageId,
        target.isEmpty() ? std::optional<std::string>{} : std::optional<std::string>{target.toStdString()});
    if (!mutation.has_value()) {
        return false;
    }
    if (m_invalidate) {
        m_invalidate(m_prepared.pageId);
    }
    return true;
}

void PageManualOverrideCommand::redo() {
    if (m_faulted) {
        return;
    }
    const QString expected = m_firstRedo ? m_prepared.beforeRef : m_prepared.beforeRef;
    m_firstRedo = false;
    if (!applyRef(expected, m_prepared.afterRef)) {
        m_faulted = true;
    }
}

void PageManualOverrideCommand::undo() {
    if (m_faulted) {
        return;
    }
    if (!applyRef(m_prepared.afterRef, m_prepared.beforeRef)) {
        m_faulted = true;
    }
}

} // namespace manga::ui::command
