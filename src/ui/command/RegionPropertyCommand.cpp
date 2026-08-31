#include "ui/command/RegionPropertyCommand.h"

namespace manga::ui::command {

namespace {

/// Shared QUndoCommand merge id for text-session edits. Only commands returning a
/// non-negative id are merge candidates; the full key check happens in mergeWith.
constexpr int kTextMergeId = 0x4D544558; // 'MTEX'

} // namespace

RegionPropertyCommand::RegionPropertyCommand(EditorCommandContext* context,
                                             TextEditSessionKey key,
                                             qint64 timestampMs,
                                             domain::PageRegionEdit before,
                                             domain::PageRegionEdit after,
                                             const QString& text)
    : RegionCommand(text), m_context(context), m_pageId(key.pageId), m_mergeKey(key), m_timestampMs(timestampMs) {
    PropertyEdit entry;
    entry.regionId = key.regionId;
    entry.before = std::move(before);
    entry.after = std::move(after);
    m_edits.append(std::move(entry));
}

RegionPropertyCommand::RegionPropertyCommand(EditorCommandContext* context,
                                             domain::PageId pageId,
                                             QVector<PropertyEdit> edits,
                                             const QString& text)
    : RegionCommand(text), m_context(context), m_pageId(pageId), m_edits(std::move(edits)) {}

int RegionPropertyCommand::id() const {
    return m_mergeKey.has_value() ? kTextMergeId : -1;
}

bool RegionPropertyCommand::mergeWith(const QUndoCommand* other) {
    const auto* rhs = dynamic_cast<const RegionPropertyCommand*>(other);
    if (rhs == nullptr || !m_mergeKey.has_value() || !rhs->m_mergeKey.has_value()) {
        return false;
    }
    if (*m_mergeKey != *rhs->m_mergeKey) {
        return false;
    }
    const qint64 delta = rhs->m_timestampMs - m_timestampMs;
    if (delta < 0 || delta > kMergeWindowMs) {
        return false;
    }
    // Keep the original before snapshot, advance to the newest after value.
    m_edits[0].after = rhs->m_edits[0].after;
    m_timestampMs = rhs->m_timestampMs;
    if (rhs->hasFaulted()) {
        // The merged-away command already replayed against the domain; its fault
        // must survive on the command that stays on the stack.
        markFaulted();
    }
    return true;
}

void RegionPropertyCommand::undo() {
    apply(false);
}

void RegionPropertyCommand::redo() {
    apply(true);
}

void RegionPropertyCommand::apply(bool useAfter) {
    std::vector<domain::PageRegionEdit> edits;
    edits.reserve(static_cast<size_t>(m_edits.size()));
    for (const auto& entry : std::as_const(m_edits)) {
        edits.push_back(useAfter ? entry.after : entry.before);
    }
    const auto result = m_context->applySnapshots(m_pageId, edits);
    if (!result.has_value()) {
        markFaulted();
    }
}

} // namespace manga::ui::command
