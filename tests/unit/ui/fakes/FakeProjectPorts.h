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
    int openPageCount{0};
    bool failCreate{false};
    QString lastCreatedPath;
    int saveCallCount{0};
    int closeCallCount{0};

    bool failOpen{false};
    QString lastOpenedPath;

    QFuture<application::Result<application::ProjectDescriptor>>
    createProject(application::CreateProjectRequest request, application::OperationContext) override {
        if (failCreate) {
            return application::makeFailedFuture<application::ProjectDescriptor>(application::makeApplicationError(
                application::ErrorCategory::Persistence, "persistence.target_not_empty", "目标目录必须为空"));
        }
        lastCreatedPath = request.path;
        return application::makeReadyFuture<application::ProjectDescriptor>(
            application::ProjectDescriptor{domain::ProjectId::generate(), request.name, request.path});
    }
    QFuture<application::Result<application::OpenProjectResult>> openProject(application::OpenProjectRequest request,
                                                                             application::OperationContext) override {
        lastOpenedPath = request.path;
        if (failOpen) {
            return application::makeFailedFuture<application::OpenProjectResult>(application::makeApplicationError(
                application::ErrorCategory::Project, "test.open_failed", "无法打开项目"));
        }
        application::OpenProjectResult result;
        domain::ComicProject project("Demo Project");
        for (int i = 0; i < openPageCount; ++i) {
            project.addPage(domain::ComicPage(domain::PageId::generate(),
                                              "assets/original/page_" + std::to_string(1000 + i) + ".png",
                                              domain::ImageSize{1200, 1800},
                                              i));
        }
        result.project = std::move(project);
        return application::makeReadyFuture<application::OpenProjectResult>(std::move(result));
    }
    QFuture<application::Result<application::OpenProjectResult>>
    recoverProject(application::OpenProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::OpenProjectResult>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.recover_failed", "stub"));
    }
    QFuture<application::Result<application::SaveProjectResult>> saveProject(application::SaveProjectRequest request,
                                                                             application::OperationContext) override {
        ++saveCallCount;
        return application::makeReadyFuture<application::SaveProjectResult>(
            application::SaveProjectResult{request.project.revision()});
    }
    QFuture<application::Result<application::SaveAsProjectResult>>
    saveProjectAs(application::SaveAsProjectRequest, application::OperationContext) override {
        return application::makeFailedFuture<application::SaveAsProjectResult>(
            application::makeApplicationError(application::ErrorCategory::Project, "test.save_as_failed", "stub"));
    }
    QFuture<application::Result<void>> closeProject(application::CloseProjectRequest,
                                                    application::OperationContext) override {
        ++closeCallCount;
        return application::makeReadyFuture<void>({});
    }
};

} // namespace manga::test
