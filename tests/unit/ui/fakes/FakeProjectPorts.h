#pragma once

#include "application/dto/ProjectDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/IRecentProjectsStore.h"
#include "domain/model/ComicProject.h"

#include <QString>
#include <QVector>

namespace manga::test {

class FakeRecentProjectsStore final : public application::IRecentProjectsStore {
public:
    [[nodiscard]] QVector<application::RecentProject> loadRecentProjects() const override {
        return m_projects;
    }
    void addRecentProject(const application::RecentProject& project) override {
        m_projects.removeIf([&](const application::RecentProject& p) { return p.path == project.path; });
        m_projects.prepend(project);
    }
    void removeRecentProject(const QString& path) override {
        m_projects.removeIf([&](const application::RecentProject& p) { return p.path == path; });
    }
    void clearRecentProjects() override {
        m_projects.clear();
    }

    QVector<application::RecentProject> m_projects;
};

class FakeProjectRepository final : public application::IProjectRepository {
public:
    bool failOpen{false};
    QString lastOpenedPath;

    QFuture<application::Result<application::ProjectDescriptor>> createProject(application::CreateProjectRequest,
                                                                               application::OperationContext) override {
        return application::makeFailedFuture<application::ProjectDescriptor>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.create_failed", "stub"));
    }
    QFuture<application::Result<application::OpenProjectResult>> openProject(application::OpenProjectRequest request,
                                                                             application::OperationContext) override {
        lastOpenedPath = request.path;
        if (failOpen) {
            return application::makeFailedFuture<application::OpenProjectResult>(application::makeApplicationError(
                application::ErrorCategory::Project, "test.open_failed", "无法打开项目"));
        }
        application::OpenProjectResult result;
        result.project = domain::ComicProject("Demo Project");
        return application::makeReadyFuture<application::OpenProjectResult>(std::move(result));
    }
    QFuture<application::Result<application::OpenProjectResult>>
    recoverProject(application::OpenProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::OpenProjectResult>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.recover_failed", "stub"));
    }
    QFuture<application::Result<application::SaveProjectResult>> saveProject(application::SaveProjectRequest,
                                                                             application::OperationContext) override {
        return application::makeFailedFuture<application::SaveProjectResult>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.save_failed", "stub"));
    }
    QFuture<application::Result<application::SaveAsProjectResult>>
    saveProjectAs(application::SaveAsProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::SaveAsProjectResult>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.save_as_failed", "stub"));
    }
    QFuture<application::Result<void>> closeProject(application::CloseProjectRequest,
                                                    application::OperationContext) override {
        return application::makeReadyFuture<void>({});
    }
};

} // namespace manga::test
