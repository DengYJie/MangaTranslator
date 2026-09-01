#include "PageClearLayerCommand.h"

#include <utility>

namespace manga::ui::command {

PageClearLayerCommand::PageClearLayerCommand(EditorCommandContext* context,
                                             application::PreparedPageInpaintChange prepared,
                                             Invalidation invalidate,
                                             QUndoCommand* parent)
    : QUndoCommand(parent), m_context(context), m_prepared(std::move(prepared)), m_invalidate(std::move(invalidate)) {
    setText(QStringLiteral("Clear Page"));
}

bool PageClearLayerCommand::applyRef(const QString& expected, const QString& target) {
    if (m_context == nullptr || m_context->project() == nullptr || !m_prepared.projectId.isValid() ||
        m_context->project()->id() != m_prepared.projectId || !m_prepared.pageId.isValid())
        return false;
    const auto* pageConst = m_context->project()->findPage(m_prepared.pageId);
    if (pageConst == nullptr || !m_prepared.expectedPageSize.isValid() ||
        pageConst->imageSize().width != m_prepared.expectedPageSize.width() ||
        pageConst->imageSize().height != m_prepared.expectedPageSize.height())
        return false;
    const QString current =
        pageConst->clearLayerAsset().has_value() ? QString::fromStdString(*pageConst->clearLayerAsset()) : QString();
    if (current != expected)
        return false;

    // ComicProject intentionally exposes page mutators only through its aggregate methods;
    // page-level layer adoption has no region to pass to applyClearChangeOnPage. The command
    // therefore performs the same atomic single-field mutation used by project rehydration,
    // followed by exactly one aggregate dirty mark.
    auto* page = const_cast<domain::ComicPage*>(pageConst);
    page->setClearLayerAsset(target.isEmpty() ? std::optional<std::string>{}
                                              : std::optional<std::string>{target.toStdString()});
    m_context->project()->markDirty();
    if (m_invalidate)
        m_invalidate(m_prepared.pageId);
    return true;
}

void PageClearLayerCommand::redo() {
    if (m_faulted)
        return;
    const QString expected = m_firstRedo ? m_prepared.beforeRef : m_prepared.beforeRef;
    m_firstRedo = false;
    if (!applyRef(expected, m_prepared.afterRef))
        m_faulted = true;
}

void PageClearLayerCommand::undo() {
    if (m_faulted)
        return;
    if (!applyRef(m_prepared.afterRef, m_prepared.beforeRef))
        m_faulted = true;
}

} // namespace manga::ui::command
