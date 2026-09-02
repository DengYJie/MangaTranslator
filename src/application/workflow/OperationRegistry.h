#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/operation/OperationDescriptor.h"
#include "application/operation/OperationId.h"
#include "application/operation/OperationSnapshot.h"
#include "domain/model/Uuid.h"

#include <QObject>
#include <QVector>

#include <memory>
#include <optional>

namespace manga::application {

/// Thread-safe, observable fact source for in-flight and recently settled
/// operations. Emits <code>operationsChanged(QVector<OperationSnapshot>)</code> on
/// the owning thread for every state change and for throttled progress updates.
///
/// Settled operations are retained until either 32 settled snapshots exist or
/// the oldest settled snapshot is older than 10 minutes, whichever comes first.
/// Active operations (queued, running, or cancelling) are never evicted.
class OperationRegistry : public QObject {
    Q_OBJECT

public:
    explicit OperationRegistry(QObject* parent = nullptr);
    ~OperationRegistry() override;

    /// Creates a new operation entry, returns an OperationContext that keeps the
    /// entry alive until the adapter/future releases it.
    [[nodiscard]] OperationContext
    createOperation(OperationDescriptor descriptor, quint64 sessionGeneration, quint64 viewGeneration);

    /// Creates an operation whose descriptor is a fixed placeholder, not a caller value. The
    /// snapshot reports stage Project with the untranslated display target Task and a nil project
    /// id, so it never matches cancelProject, which selects on the descriptor project id.
    [[nodiscard]] OperationContext createOperation(quint64 sessionGeneration, quint64 viewGeneration);

    /// Returns a consistent snapshot of all known operations.
    [[nodiscard]] QVector<OperationSnapshot> snapshot() const;

    void cancelOperation(OperationId id);
    void cancelProject(domain::ProjectId projectId);
    void cancelAll();

    /// Transitions a queued operation to running. Safe to call from any thread.
    void markRunning(OperationId id);

    /// Idempotent settlement. If the operation is currently cancelling, it
    /// becomes Cancelled. An error with category Cancelled also becomes Cancelled.
    /// A non-empty reasonCode without an error becomes Skipped (or Stale when the
    /// code starts with "stale.").
    void markFinished(OperationId id, std::optional<ApplicationError> error = std::nullopt, QString reasonCode = {});

    /// Convenience settlement helpers.
    void markSkipped(OperationId id, QString reasonCode);
    void markStale(OperationId id, QString reasonCode = QStringLiteral("stale.session_changed"));

    [[nodiscard]] qsizetype inFlightCount() const;

Q_SIGNALS:
    void operationsChanged(QVector<OperationSnapshot> snapshots);

public Q_SLOTS:
    /// Re-publishes the current snapshot. Called directly on the owning thread
    /// or queued from worker threads.
    void requestPublish();

private:
    struct Entry;
    struct State;

    static QVector<OperationSnapshot> buildSnapshotLocked(const State* state);
    static void settleEntry(Entry& entry, const std::optional<ApplicationError>& error, const QString& reasonCode);
    static void evictSettledLocked(State* state);

    std::shared_ptr<State> m_state;
};

} // namespace manga::application
