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
    refresh();
}

void HomeViewModel::refresh() {
    if (m_recentStore == nullptr) {
        return;
    }
    m_coverStopSource.request_stop();
    m_coverStopSource = std::stop_source{};
    const auto generation = ++m_coverGeneration;
    const auto projects = m_recentStore->loadRecentProjects();
    updateState([&](HomeState& state) {
        state.recentProjects = projects;
        QHash<QString, QImage> covers;
        for (const auto& project : projects) {
            const auto existing = state.projectCovers.constFind(project.path);
            if (existing != state.projectCovers.cend()) {
                covers.insert(project.path, *existing);
            }
        }
        state.projectCovers = std::move(covers);
    });
    loadNextCover(0, generation);
}

HomeViewModel::~HomeViewModel() {
    m_coverStopSource.request_stop();
}

void HomeViewModel::loadNextCover(qsizetype index, quint64 generation) {
    if (generation != m_coverGeneration || index >= m_state.recentProjects.size()) {
        return;
    }
    const QString path = m_state.recentProjects.at(index).path;
    application::OperationContext context;
    context.stopToken = m_coverStopSource.get_token();
    auto* watcher = new QFutureWatcher<application::Result<QImage>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path, index, generation] {
        watcher->deleteLater();
        if (generation != m_coverGeneration) {
            return;
        }
        const auto result = watcher->result();
        updateState([&](HomeState& state) {
            if (result && !result->isNull()) {
                state.projectCovers.insert(path, *result);
            } else {
                state.projectCovers.remove(path);
            }
        });
        loadNextCover(index + 1, generation);
    });
    watcher->setFuture(m_recentStore->loadProjectCover(path, context));
}

void HomeViewModel::requestOpenProjectPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (m_state.isOpeningProject || trimmed.isEmpty()) {
        return;
    }
    Q_EMIT projectOpenRequested(trimmed);
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
    auto result = m_openWatcher.result();
    updateState([](HomeState& state) { state.isOpeningProject = false; });
    if (result.has_value()) {
        auto value = std::move(result).value();
        application::OpenedProjectSession session;
        session.project = std::move(value.project);
        session.projectRootPath = m_pendingOpenPath;
        session.recoverySource = value.recoverySource;
        session.warnings = std::move(value.warnings);
        const QString name = QString::fromStdString(session.project.name());
        m_recentStore->addRecentProject({name, m_pendingOpenPath, QDateTime::currentMSecsSinceEpoch()});
        refresh();
        Q_EMIT toastRequested(tr("已打开项目「%1」").arg(name), false);
        Q_EMIT projectOpened(session);
    } else {
        const auto& error = result.error();
        const auto presentation = m_recoveryPresenter.present(error, m_pendingOpenPath);
        if (presentation) {
            QString displayMsg = presentation->safeMessage;
            if (presentation->occurrenceCount > 1) {
                displayMsg = tr("%1 (%2 次)").arg(presentation->safeMessage).arg(presentation->occurrenceCount);
            }
            const QString retryPath = m_pendingOpenPath;
            updateState([&](HomeState& state) {
                state.notice = HomeNotice{
                    .severity = HomeNotice::Severity::Error,
                    .message = displayMsg,
                    .retryPath = retryPath,
                    .actionText = presentation->actionText,
                    .action = presentation->action,
                    .occurrenceCount = presentation->occurrenceCount,
                };
            });
        }
    }
}

} // namespace manga::ui::screen::home
