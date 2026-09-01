#pragma once

#include "domain/model/Uuid.h"

#include <QDeadlineTimer>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <expected>
#include <map>
#include <memory>
#include <optional>

namespace manga::data {

/// Serializes project write operations per canonical project root inside one process and hands
/// out read leases that closeProject waits on. Owned by the composition root and injected into
/// the repository, importer, and page image loader; it never crosses an application port and
/// does not replace the inter-process QLockFile.
class ProjectIoCoordinator {
private:
    struct RootState;

public:
    enum class AcquireFailure {
        NotOpen,
        ProjectMismatch,
        RootClosing,
    };

    enum class CloseFailure {
        NotOpen,
        Timeout,
    };

    /// Held for the duration of one write operation on a project root. Blocks other write
    /// operations on the same root until released.
    class WriteTicket {
    public:
        WriteTicket() noexcept = default;
        WriteTicket(WriteTicket&& other) noexcept;
        WriteTicket& operator=(WriteTicket&& other) noexcept;
        ~WriteTicket();
        void release() noexcept;
        [[nodiscard]] bool isActive() const noexcept {
            return m_state != nullptr;
        }

    private:
        friend class ProjectIoCoordinator;
        explicit WriteTicket(std::shared_ptr<RootState> state) noexcept;
        std::shared_ptr<RootState> m_state;
    };

    /// Held for the duration of one read operation (page image loading). closeProject waits
    /// until every lease on the root is released.
    class SessionLease {
    public:
        SessionLease() noexcept = default;
        SessionLease(SessionLease&& other) noexcept;
        SessionLease& operator=(SessionLease&& other) noexcept;
        ~SessionLease();
        void release() noexcept;
        [[nodiscard]] bool isActive() const noexcept {
            return m_state != nullptr;
        }

    private:
        friend class ProjectIoCoordinator;
        explicit SessionLease(std::shared_ptr<RootState> state) noexcept;
        std::shared_ptr<RootState> m_state;
    };

    /// Blocks until other write operations on the root finish and rejects when the root is
    /// closing or closed.
    [[nodiscard]] std::expected<WriteTicket, AcquireFailure> beginWrite(const QString& canonicalRoot);
    /// Acquires a write ticket only when projectId is bound to canonicalRoot by the repository.
    [[nodiscard]] std::expected<WriteTicket, AcquireFailure> beginProjectWrite(const domain::ProjectId& projectId,
                                                                               const QString& canonicalRoot);
    /// Acquires a read lease only when projectId is bound to canonicalRoot by the repository.
    [[nodiscard]] std::expected<SessionLease, AcquireFailure> acquireProjectLease(const domain::ProjectId& projectId,
                                                                                  const QString& canonicalRoot);
    /// Opens asset access for a repository session after project validation succeeds.
    [[nodiscard]] bool bindProject(const domain::ProjectId& projectId, const QString& canonicalRoot);
    /// Stops new asset access before a session moves away from a root.
    void unbindProject(const QString& canonicalRoot);
    /// Marks the root closing, waits for active leases, and returns a ticket that releases the
    /// write lock. A timeout restores the open state so the caller can retry safely.
    [[nodiscard]] std::expected<WriteTicket, CloseFailure> beginClose(const QString& canonicalRoot, int timeoutMs);
    /// Drops the runtime state for a root so a later operation on the same path starts fresh.
    void discardRoot(const QString& canonicalRoot);

private:
    struct RootState {
        QMutex writeMutex;
        QMutex stateMutex;
        int leases{0};
        bool closed{false};
        // Asset ports acquire tickets only while this binding matches their request.
        bool projectBound{false};
        domain::ProjectId projectId;
        QWaitCondition noLeases;
    };

    [[nodiscard]] std::shared_ptr<RootState> stateFor(const QString& canonicalRoot);
    [[nodiscard]] std::shared_ptr<RootState> findState(const QString& canonicalRoot);

    mutable QMutex m_mapMutex;
    std::map<QString, std::shared_ptr<RootState>> m_roots;
};

} // namespace manga::data
