#include "application/workflow/OperationRegistry.h"

#include "application/operation/OperationProgressReporter.h"

#include <QDateTime>
#include <QHash>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <limits>
#include <list>
#include <utility>

namespace manga::application {

namespace {

constexpr qint64 kSettledRetentionMinutes = 10;
constexpr qint64 kSettledRetentionMs = kSettledRetentionMinutes * 60 * 1000;
constexpr qsizetype kMaxSettledSnapshots = 32;

QString keyFor(OperationId id) {
    return QString::fromStdString(id.toString());
}

bool isSettled(OperationState state) {
    switch (state) {
        case OperationState::Completed:
        case OperationState::Failed:
        case OperationState::Cancelled:
        case OperationState::Skipped:
        case OperationState::Stale:
            return true;
        case OperationState::Queued:
        case OperationState::Running:
        case OperationState::Cancelling:
            return false;
    }
    return false;
}

ApplicationError cancelledError() {
    return makeApplicationError(
        ErrorCategory::Cancelled, QStringLiteral("cancelled.user_requested"), QStringLiteral("The task was cancelled"));
}

void scheduleEmit(OperationRegistry* registry) {
    if (!registry) {
        return;
    }
    if (QThread::currentThread() == registry->thread()) {
        registry->requestPublish();
    } else {
        QMetaObject::invokeMethod(registry, &OperationRegistry::requestPublish, Qt::QueuedConnection);
    }
}

} // namespace

struct OperationRegistry::Entry {
    OperationId id;
    OperationDescriptor descriptor;
    quint64 sessionGeneration{0};
    quint64 viewGeneration{0};
    OperationState state{OperationState::Queued};
    qint64 completed{0};
    qint64 total{0};
    QString statusText;
    std::optional<ApplicationError> error;
    QString reasonCode;
    qint64 settledAtMs{-1};
    std::shared_ptr<std::stop_source> stopSource;
    std::shared_ptr<OperationProgressReporter> reporter;
};

struct OperationRegistry::State {
    mutable QMutex mutex;
    std::list<Entry> operations;
    QHash<QString, std::list<Entry>::iterator> index;
    OperationRegistry* registry{nullptr};
};

QVector<OperationSnapshot> OperationRegistry::buildSnapshotLocked(const State* state) {
    QVector<OperationSnapshot> result;
    result.reserve(static_cast<qsizetype>(state->operations.size()));
    for (const auto& entry : state->operations) {
        OperationSnapshot snapshot;
        snapshot.id = entry.id;
        snapshot.descriptor = entry.descriptor;
        snapshot.state = entry.state;
        snapshot.completed = entry.completed;
        snapshot.total = entry.total;
        snapshot.statusText = entry.statusText;
        snapshot.error = entry.error;
        snapshot.reasonCode = entry.reasonCode;
        result.push_back(std::move(snapshot));
    }
    return result;
}

void OperationRegistry::settleEntry(Entry& entry,
                                    const std::optional<ApplicationError>& error,
                                    const QString& reasonCode) {
    if (isSettled(entry.state)) {
        return;
    }

    if (entry.state == OperationState::Cancelling || (error && error->category == ErrorCategory::Cancelled)) {
        entry.state = OperationState::Cancelled;
        entry.error = error.value_or(cancelledError());
    } else if (error) {
        entry.state = OperationState::Failed;
        entry.error = *error;
    } else if (!reasonCode.isEmpty()) {
        entry.reasonCode = reasonCode;
        if (reasonCode.startsWith(QStringLiteral("stale."))) {
            entry.state = OperationState::Stale;
        } else {
            entry.state = OperationState::Skipped;
        }
    } else {
        entry.state = OperationState::Completed;
    }

    entry.settledAtMs = QDateTime::currentMSecsSinceEpoch();
}

void OperationRegistry::evictSettledLocked(State* state) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = now - kSettledRetentionMs;

    std::vector<std::list<Entry>::iterator> settled;

    auto it = state->operations.begin();
    while (it != state->operations.end()) {
        if (isSettled(it->state)) {
            if (it->settledAtMs < cutoff) {
                state->index.remove(keyFor(it->id));
                it = state->operations.erase(it);
                continue;
            } else {
                settled.push_back(it);
            }
        }
        ++it;
    }

    if (settled.size() > kMaxSettledSnapshots) {
        std::sort(settled.begin(), settled.end(), [](const auto& a, const auto& b) {
            return a->settledAtMs < b->settledAtMs;
        });
        size_t toRemove = settled.size() - kMaxSettledSnapshots;
        for (size_t i = 0; i < toRemove; ++i) {
            state->index.remove(keyFor(settled[i]->id));
            state->operations.erase(settled[i]);
        }
    }
}

OperationRegistry::OperationRegistry(QObject* parent) : QObject(parent), m_state(std::make_shared<State>()) {
    m_state->registry = this;
}

OperationRegistry::~OperationRegistry() {
    QMutexLocker locker(&m_state->mutex);
    m_state->registry = nullptr;
}

OperationContext
OperationRegistry::createOperation(OperationDescriptor descriptor, quint64 sessionGeneration, quint64 viewGeneration) {
    QMutexLocker locker(&m_state->mutex);

    Entry entry;
    entry.id = OperationId::generate();
    entry.descriptor = std::move(descriptor);
    entry.sessionGeneration = sessionGeneration;
    entry.viewGeneration = viewGeneration;
    entry.state = OperationState::Queued;
    entry.stopSource = std::make_shared<std::stop_source>();

    const QString key = keyFor(entry.id);
    const auto it = m_state->operations.insert(m_state->operations.end(), std::move(entry));
    m_state->index.insert(key, it);

    const OperationId id = it->id;

    auto progressPublisher = [stateWeak = std::weak_ptr<State>(m_state),
                              id](qint64 completed, qint64 total, const QString& statusText, bool /*force*/) {
        if (auto state = stateWeak.lock()) {
            bool changed = false;
            OperationRegistry* regToEmit = nullptr;
            {
                QMutexLocker locker(&state->mutex);
                const auto mapIt = state->index.find(keyFor(id));
                if (mapIt == state->index.end()) {
                    return;
                }
                auto& entry = *mapIt.value();
                entry.completed = completed;
                entry.total = total;
                if (!statusText.isEmpty()) {
                    entry.statusText = statusText;
                }
                changed = true;
                regToEmit = state->registry;
            }
            if (changed && regToEmit) {
                QMetaObject::invokeMethod(regToEmit, &OperationRegistry::requestPublish, Qt::QueuedConnection);
            }
        }
    };

    auto runningPublisher = [stateWeak = std::weak_ptr<State>(m_state), id]() {
        if (auto state = stateWeak.lock()) {
            bool changed = false;
            OperationRegistry* regToEmit = nullptr;
            {
                QMutexLocker locker(&state->mutex);
                const auto mapIt = state->index.find(keyFor(id));
                if (mapIt == state->index.end()) {
                    return;
                }
                auto& entry = *mapIt.value();
                if (entry.state == OperationState::Queued) {
                    entry.state = OperationState::Running;
                    changed = true;
                }
                regToEmit = state->registry;
            }
            if (changed && regToEmit) {
                QMetaObject::invokeMethod(regToEmit, &OperationRegistry::requestPublish, Qt::QueuedConnection);
            }
        }
    };

    auto completionPublisher = [stateWeak = std::weak_ptr<State>(m_state), id](std::optional<ApplicationError> error,
                                                                               const QString& reasonCode) {
        if (auto state = stateWeak.lock()) {
            bool changed = false;
            OperationRegistry* regToEmit = nullptr;
            {
                QMutexLocker locker(&state->mutex);
                const auto mapIt = state->index.find(keyFor(id));
                if (mapIt == state->index.end()) {
                    return;
                }
                auto& entry = *mapIt.value();
                if (!isSettled(entry.state)) {
                    OperationRegistry::settleEntry(entry, error, reasonCode);
                    OperationRegistry::evictSettledLocked(state.get());
                    changed = true;
                }
                regToEmit = state->registry;
            }
            if (changed && regToEmit) {
                QMetaObject::invokeMethod(regToEmit, &OperationRegistry::requestPublish, Qt::QueuedConnection);
            }
        }
    };

    it->reporter =
        std::make_shared<OperationProgressReporter>(progressPublisher, runningPublisher, completionPublisher);

    OperationContext context;
    context.operationId = id;
    context.sessionGeneration = sessionGeneration;
    context.viewGeneration = viewGeneration;
    context.stopToken = it->stopSource->get_token();
    context.progressReporter = it->reporter;
    context.lifetime = std::shared_ptr<void>(new char, [state = m_state, key](void* value) {
        delete static_cast<char*>(value);
        bool changed = false;
        OperationRegistry* regToEmit = nullptr;
        {
            QMutexLocker locker(&state->mutex);
            const auto mapIt = state->index.find(key);
            if (mapIt != state->index.end()) {
                auto& entry = *mapIt.value();
                if (!isSettled(entry.state)) {
                    entry.state = OperationState::Cancelled;
                    entry.error = cancelledError();
                    entry.settledAtMs = QDateTime::currentMSecsSinceEpoch();
                    OperationRegistry::evictSettledLocked(state.get());
                    changed = true;
                }
            }
            regToEmit = state->registry;
        }
        if (changed && regToEmit) {
            QMetaObject::invokeMethod(regToEmit, &OperationRegistry::requestPublish, Qt::QueuedConnection);
        }
    });

    locker.unlock();
    scheduleEmit(this);
    return context;
}

OperationContext OperationRegistry::createOperation(quint64 sessionGeneration, quint64 viewGeneration) {
    OperationDescriptor descriptor{
        .stage = OperationStage::Project,
        .displayTarget = QStringLiteral("Task"),
    };
    return createOperation(std::move(descriptor), sessionGeneration, viewGeneration);
}

QVector<OperationSnapshot> OperationRegistry::snapshot() const {
    QMutexLocker locker(&m_state->mutex);
    return buildSnapshotLocked(m_state.get());
}

void OperationRegistry::cancelOperation(OperationId id) {
    bool changed = false;
    {
        QMutexLocker locker(&m_state->mutex);
        const auto mapIt = m_state->index.find(keyFor(id));
        if (mapIt == m_state->index.end()) {
            return;
        }
        auto& entry = *mapIt.value();
        if (isSettled(entry.state)) {
            return;
        }
        entry.state = OperationState::Cancelling;
        entry.stopSource->request_stop();
        changed = true;
    }
    if (changed) {
        scheduleEmit(this);
    }
}

void OperationRegistry::cancelProject(domain::ProjectId projectId) {
    bool changed = false;
    {
        QMutexLocker locker(&m_state->mutex);
        for (auto& entry : m_state->operations) {
            if (entry.descriptor.projectId == projectId && !isSettled(entry.state)) {
                entry.state = OperationState::Cancelling;
                entry.stopSource->request_stop();
                changed = true;
            }
        }
    }
    if (changed) {
        scheduleEmit(this);
    }
}

void OperationRegistry::markRunning(OperationId id) {
    bool changed = false;
    {
        QMutexLocker locker(&m_state->mutex);
        const auto mapIt = m_state->index.find(keyFor(id));
        if (mapIt == m_state->index.end()) {
            return;
        }
        auto& entry = *mapIt.value();
        if (entry.state != OperationState::Queued) {
            return;
        }
        entry.state = OperationState::Running;
        changed = true;
    }
    if (changed) {
        scheduleEmit(this);
    }
}

void OperationRegistry::markFinished(OperationId id, std::optional<ApplicationError> error, QString reasonCode) {
    bool changed = false;
    {
        QMutexLocker locker(&m_state->mutex);
        const auto mapIt = m_state->index.find(keyFor(id));
        if (mapIt == m_state->index.end()) {
            return;
        }
        auto& entry = *mapIt.value();
        if (isSettled(entry.state)) {
            return;
        }
        settleEntry(entry, error, reasonCode);
        evictSettledLocked(m_state.get());
        changed = true;
    }
    if (changed) {
        scheduleEmit(this);
    }
}

void OperationRegistry::markSkipped(OperationId id, QString reasonCode) {
    markFinished(id, std::nullopt, std::move(reasonCode));
}

void OperationRegistry::markStale(OperationId id, QString reasonCode) {
    if (!reasonCode.startsWith(QStringLiteral("stale."))) {
        reasonCode = QStringLiteral("stale.") + reasonCode;
    }
    markFinished(id, std::nullopt, std::move(reasonCode));
}

qsizetype OperationRegistry::inFlightCount() const {
    QMutexLocker locker(&m_state->mutex);
    qsizetype count = 0;
    for (const auto& entry : m_state->operations) {
        if (!isSettled(entry.state)) {
            ++count;
        }
    }
    return count;
}

void OperationRegistry::requestPublish() {
    QVector<OperationSnapshot> snapshots;
    {
        QMutexLocker locker(&m_state->mutex);
        snapshots = buildSnapshotLocked(m_state.get());
    }
    Q_EMIT operationsChanged(std::move(snapshots));
}

} // namespace manga::application
