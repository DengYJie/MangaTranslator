#pragma once

#include "ui/command/RegionPropertyCommand.h"

#include <QElapsedTimer>

#include <functional>
#include <optional>

namespace manga::ui::screen::editor {

/// Tracks the active text edit batch for merge-aware undo commands.
///
/// One batch is active at a time, identified by project/page/region/field. A new
/// batch starts when the identity changes or after seal(); session ids increase
/// monotonically and are never reused, so a sealed batch can never merge again.
/// The clock is injectable; production uses a process-local QElapsedTimer and
/// tests drive time explicitly. All timestamps are monotonic, never wall clock.
class TextEditSession {
public:
    using Clock = std::function<qint64()>;

    explicit TextEditSession(Clock clock = {});

    /// Returns the merge key for an edit. Starts a new session when no active
    /// session matches the given identity.
    [[nodiscard]] command::TextEditSessionKey keyFor(const domain::ProjectId& projectId,
                                                     const domain::PageId& pageId,
                                                     const domain::RegionId& regionId,
                                                     command::RegionPropertyField field);
    [[nodiscard]] qint64 nowMs() const;
    [[nodiscard]] bool hasActiveSession() const noexcept {
        return m_active.has_value();
    }

    /// Ends the active batch; the next keyFor starts a fresh session.
    void seal();
    /// Drops all session state (project close/open).
    void reset();

private:
    struct ActiveIdentity {
        domain::ProjectId projectId;
        domain::PageId pageId;
        domain::RegionId regionId;
        command::RegionPropertyField field{command::RegionPropertyField::SourceText};

        bool operator==(const ActiveIdentity&) const = default;
    };

    Clock m_clock;
    std::optional<ActiveIdentity> m_active;
    quint64 m_activeSessionId{0};
    quint64 m_nextSessionId{1};
};

} // namespace manga::ui::screen::editor
