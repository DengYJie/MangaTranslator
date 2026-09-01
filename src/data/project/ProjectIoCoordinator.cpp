#include "data/project/ProjectIoCoordinator.h"

#include <QDir>

#include <algorithm>

namespace manga::data {
namespace {

QString rootKey(QString root) {
    root = QDir::cleanPath(QDir::fromNativeSeparators(root));
#ifdef Q_OS_WIN
    root = root.toCaseFolded();
#endif
    return root;
}

} // namespace

ProjectIoCoordinator::WriteTicket::WriteTicket(WriteTicket&& other) noexcept : m_state(std::move(other.m_state)) {}

ProjectIoCoordinator::WriteTicket& ProjectIoCoordinator::WriteTicket::operator=(WriteTicket&& other) noexcept {
    if (this != &other) {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

ProjectIoCoordinator::WriteTicket::~WriteTicket() {
    release();
}

ProjectIoCoordinator::WriteTicket::WriteTicket(std::shared_ptr<RootState> state) noexcept : m_state(std::move(state)) {}

void ProjectIoCoordinator::WriteTicket::release() noexcept {
    if (m_state) {
        m_state->writeMutex.unlock();
        m_state.reset();
    }
}

ProjectIoCoordinator::SessionLease::SessionLease(SessionLease&& other) noexcept : m_state(std::move(other.m_state)) {}

ProjectIoCoordinator::SessionLease& ProjectIoCoordinator::SessionLease::operator=(SessionLease&& other) noexcept {
    if (this != &other) {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

ProjectIoCoordinator::SessionLease::~SessionLease() {
    release();
}

ProjectIoCoordinator::SessionLease::SessionLease(std::shared_ptr<RootState> state) noexcept
    : m_state(std::move(state)) {}

void ProjectIoCoordinator::SessionLease::release() noexcept {
    if (m_state) {
        {
            QMutexLocker locker(&m_state->stateMutex);
            --m_state->leases;
            if (m_state->leases <= 0) {
                m_state->noLeases.wakeAll();
            }
        }
        m_state.reset();
    }
}

std::shared_ptr<ProjectIoCoordinator::RootState> ProjectIoCoordinator::stateFor(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mapMutex);
    const QString key = rootKey(canonicalRoot);
    auto it = m_roots.find(key);
    if (it != m_roots.end()) {
        return it->second;
    }
    auto state = std::make_shared<RootState>();
    m_roots.emplace(key, state);
    return state;
}

std::shared_ptr<ProjectIoCoordinator::RootState> ProjectIoCoordinator::findState(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mapMutex);
    auto it = m_roots.find(rootKey(canonicalRoot));
    return it != m_roots.end() ? it->second : nullptr;
}

std::expected<ProjectIoCoordinator::WriteTicket, ProjectIoCoordinator::AcquireFailure>
ProjectIoCoordinator::beginProjectWrite(const domain::ProjectId& projectId, const QString& canonicalRoot) {
    auto state = findState(canonicalRoot);
    if (!state) {
        return std::unexpected(AcquireFailure::NotOpen);
    }
    state->writeMutex.lock();
    QMutexLocker locker(&state->stateMutex);
    if (state->closed) {
        state->writeMutex.unlock();
        return std::unexpected(AcquireFailure::RootClosing);
    }
    if (!state->projectBound) {
        state->writeMutex.unlock();
        return std::unexpected(AcquireFailure::NotOpen);
    }
    if (state->projectId != projectId) {
        state->writeMutex.unlock();
        return std::unexpected(AcquireFailure::ProjectMismatch);
    }
    return WriteTicket(state);
}

std::expected<ProjectIoCoordinator::SessionLease, ProjectIoCoordinator::AcquireFailure>
ProjectIoCoordinator::acquireProjectLease(const domain::ProjectId& projectId, const QString& canonicalRoot) {
    auto state = findState(canonicalRoot);
    if (!state) {
        return std::unexpected(AcquireFailure::NotOpen);
    }
    QMutexLocker locker(&state->stateMutex);
    if (state->closed) {
        return std::unexpected(AcquireFailure::RootClosing);
    }
    if (!state->projectBound) {
        return std::unexpected(AcquireFailure::NotOpen);
    }
    if (state->projectId != projectId) {
        return std::unexpected(AcquireFailure::ProjectMismatch);
    }
    ++state->leases;
    return SessionLease(state);
}

bool ProjectIoCoordinator::bindProject(const domain::ProjectId& projectId, const QString& canonicalRoot) {
    auto state = stateFor(canonicalRoot);
    QMutexLocker locker(&state->stateMutex);
    if (state->closed || (state->projectBound && state->projectId != projectId)) {
        return false;
    }
    state->projectId = projectId;
    state->projectBound = true;
    return true;
}

void ProjectIoCoordinator::unbindProject(const QString& canonicalRoot) {
    auto state = findState(canonicalRoot);
    if (!state) {
        return;
    }
    QMutexLocker locker(&state->stateMutex);
    state->projectBound = false;
}

std::expected<ProjectIoCoordinator::WriteTicket, ProjectIoCoordinator::AcquireFailure>
ProjectIoCoordinator::beginWrite(const QString& canonicalRoot) {
    auto state = stateFor(canonicalRoot);
    state->writeMutex.lock();
    {
        QMutexLocker locker(&state->stateMutex);
        if (state->closed) {
            state->writeMutex.unlock();
            return std::unexpected(AcquireFailure::RootClosing);
        }
    }
    return WriteTicket(state);
}

std::expected<ProjectIoCoordinator::WriteTicket, ProjectIoCoordinator::CloseFailure>
ProjectIoCoordinator::beginClose(const QString& canonicalRoot, int timeoutMs) {
    auto state = findState(canonicalRoot);
    if (!state) {
        return std::unexpected(CloseFailure::NotOpen);
    }
    QDeadlineTimer deadline(std::max(timeoutMs, 0));
    if (!state->writeMutex.tryLock(deadline)) {
        return std::unexpected(CloseFailure::Timeout);
    }
    QMutexLocker locker(&state->stateMutex);
    state->closed = true;
    while (state->leases > 0) {
        if (!state->noLeases.wait(&state->stateMutex, deadline)) {
            state->closed = false;
            locker.unlock();
            state->writeMutex.unlock();
            return std::unexpected(CloseFailure::Timeout);
        }
    }
    return WriteTicket(state);
}

void ProjectIoCoordinator::discardRoot(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mapMutex);
    m_roots.erase(rootKey(canonicalRoot));
}

} // namespace manga::data
