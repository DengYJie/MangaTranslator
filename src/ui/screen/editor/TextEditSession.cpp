#include "ui/screen/editor/TextEditSession.h"

namespace manga::ui::screen::editor {

TextEditSession::TextEditSession(Clock clock) : m_clock(std::move(clock)) {
    if (!m_clock) {
        // Function-local so the elapsed origin is process-wide and monotonic.
        m_clock = []() {
            static const QElapsedTimer timer = [] {
                QElapsedTimer t;
                t.start();
                return t;
            }();
            return timer.elapsed();
        };
    }
}

command::TextEditSessionKey TextEditSession::keyFor(const domain::ProjectId& projectId,
                                                    const domain::PageId& pageId,
                                                    const domain::RegionId& regionId,
                                                    command::RegionPropertyField field) {
    const ActiveIdentity identity{projectId, pageId, regionId, field};
    if (!m_active.has_value() || *m_active != identity) {
        m_active = identity;
        m_activeSessionId = m_nextSessionId++;
    }
    return command::TextEditSessionKey{projectId, pageId, regionId, field, m_activeSessionId};
}

qint64 TextEditSession::nowMs() const {
    return m_clock();
}

void TextEditSession::seal() {
    m_active.reset();
}

void TextEditSession::reset() {
    m_active.reset();
}

} // namespace manga::ui::screen::editor
