#pragma once

#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "ui/base/BaseViewModel.h"
#include "ui/error/RecoveryPresenter.h"
#include "ui/screen/home/HomeState.h"

#include <QFutureWatcher>

namespace manga::application {
class IProjectRepository;
class IRecentProjectsStore;
class OperationRegistry;
} // namespace manga::application

namespace manga::ui::screen::home {

/// Drives the Home surface: recent projects list and open-project flow.
class HomeViewModel : public base::BaseViewModel<HomeViewModel, HomeState> {
    Q_OBJECT
public:
    HomeViewModel(application::IRecentProjectsStore* recentStore,
                  application::IProjectRepository* projectRepository,
                  application::OperationRegistry* operationRegistry,
                  QObject* parent = nullptr);

    void refresh();
    void requestOpenProjectPath(const QString& path);
    void openProjectPath(const QString& path);
    void removeRecent(const QString& path);
    void dismissNotice();
    void requestOpenProjectDialog() {
        Q_EMIT openProjectDialogRequested();
    }

Q_SIGNALS:
    void stateChanged(const HomeState& state);
    void toastRequested(const QString& message, bool isError);
    void openProjectDialogRequested();
    void projectOpenRequested(const QString& path);
    void projectOpened(const application::OpenedProjectSession& session);

protected:
    void emitStateChanged() override {
        Q_EMIT stateChanged(m_state);
    }

private:
    void handleOpenFinished();

    application::IRecentProjectsStore* m_recentStore;     // non-owning
    application::IProjectRepository* m_projectRepository; // non-owning
    application::OperationRegistry* m_operationRegistry;  // non-owning
    ui::error::RecoveryPresenter m_recoveryPresenter;
    QFutureWatcher<application::Result<application::OpenProjectResult>> m_openWatcher;
    quint64 m_openToken{0};
    QString m_pendingOpenPath;
};

} // namespace manga::ui::screen::home
