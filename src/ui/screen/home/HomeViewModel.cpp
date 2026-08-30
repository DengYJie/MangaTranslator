#include "ui/screen/home/HomeViewModel.h"

#include "application/dto/ProjectDto.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "application/workflow/OperationRegistry.h"

#include <QDateTime>

namespace manga::ui::screen::home {

HomeViewModel::HomeViewModel(application::IRecentProjectsStore* recentStore,
                             application::IProjectRepository* projectRepository,
                             application::OperationRegistry* operationRegistry,
                             QObject* parent)
    : base::BaseViewModel<HomeViewModel, HomeState>(parent), m_recentStore(recentStore),
      m_projectRepository(projectRepository), m_operationRegistry(operationRegistry) {
    connect(&m_openWatcher, &QFutureWatcherBase::finished, this, [this]() { handleOpenFinished(); });
}

void HomeViewModel::refresh() {
    updateState([this](HomeState& state) { state.recentProjects = m_recentStore->loadRecentProjects(); });
}

void HomeViewModel::openProjectPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (m_state.isOpeningProject || trimmed.isEmpty()) {
        return;
    }
    m_pendingOpenPath = trimmed;
    m_openToken = beginRequest();
    updateState([](HomeState& state) {
        state.isOpeningProject = true;
        state.notice.reset();
    });
    const auto context = m_operationRegistry->createOperation(0, 0);
    m_openWatcher.setFuture(m_projectRepository->openProject(application::OpenProjectRequest{trimmed}, context));
}

void HomeViewModel::removeRecent(const QString& path) {
    m_recentStore->removeRecentProject(path);
    refresh();
}

void HomeViewModel::dismissNotice() {
    updateState([](HomeState& state) { state.notice.reset(); });
}

void HomeViewModel::handleOpenFinished() {
    if (!isRequestCurrent(m_openToken)) {
        return; // Stale completion.
    }
    const auto result = m_openWatcher.result();
    updateState([](HomeState& state) { state.isOpeningProject = false; });
    if (result.has_value()) {
        application::OpenedProjectSession session;
        session.project = result.value().project;
        session.projectRootPath = m_pendingOpenPath;
        session.recoverySource = result.value().recoverySource;
        session.warnings = result.value().warnings;
        const QString name = QString::fromStdString(session.project.name());
        m_recentStore->addRecentProject({name, m_pendingOpenPath, QDateTime::currentMSecsSinceEpoch()});
        refresh();
        Q_EMIT toastRequested(tr("已打开项目「%1」").arg(name), false);
        Q_EMIT projectOpened(session);
    } else {
        const QString safeMessage = result.error().safeMessage;
        const QString retryPath = m_pendingOpenPath;
        updateState(
            [&](HomeState& state) { state.notice = HomeNotice{HomeNotice::Severity::Error, safeMessage, retryPath}; });
    }
}

} // namespace manga::ui::screen::home
