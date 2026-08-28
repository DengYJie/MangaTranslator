#include "data/project/ProjectIoCoordinator.h"

namespace manga::data {

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
    auto it = m_roots.find(canonicalRoot);
    if (it != m_roots.end()) {
        return it->second;
    }
    auto state = std::make_shared<RootState>();
    m_roots.emplace(canonicalRoot, state);
    return state;
}

std::shared_ptr<ProjectIoCoordinator::RootState> ProjectIoCoordinator::findState(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mapMutex);
    auto it = m_roots.find(canonicalRoot);
    return it != m_roots.end() ? it->second : nullptr;
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

std::optional<ProjectIoCoordinator::SessionLease> ProjectIoCoordinator::acquireLease(const QString& canonicalRoot) {
    auto state = stateFor(canonicalRoot);
    QMutexLocker locker(&state->stateMutex);
    if (state->closed) {
        return std::nullopt;
    }
    ++state->leases;
    return SessionLease(state);
}

std::optional<ProjectIoCoordinator::WriteTicket> ProjectIoCoordinator::beginClose(const QString& canonicalRoot) {
    auto state = findState(canonicalRoot);
    if (!state) {
        return std::nullopt;
    }
    state->writeMutex.lock();
    QMutexLocker locker(&state->stateMutex);
    state->closed = true;
    while (state->leases > 0) {
        state->noLeases.wait(&state->stateMutex);
    }
    return WriteTicket(state);
}

void ProjectIoCoordinator::discardRoot(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mapMutex);
    m_roots.erase(canonicalRoot);
}

} // namespace manga::data
