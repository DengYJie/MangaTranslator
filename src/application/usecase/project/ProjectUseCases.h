#pragma once

#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IProjectRepository.h"

#include <utility>

namespace manga::application {
namespace detail {

template <typename T> [[nodiscard]] QFuture<Result<T>> invalidProjectRequest(QString code, QString message) {
    return makeFailedFuture<T>(makeApplicationError(ErrorCategory::Input, std::move(code), std::move(message)));
}

} // namespace detail

class CreateProjectUseCase {
public:
    explicit CreateProjectUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<ProjectDescriptor>> execute(const CreateProjectRequest& request,
                                                             const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<ProjectDescriptor>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<ProjectDescriptor>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (request.name.trimmed().isEmpty() || request.path.trimmed().isEmpty() || request.sourceLanguage.empty() ||
            request.targetLanguage.empty()) {
            return detail::invalidProjectRequest<ProjectDescriptor>("input.invalid_project",
                                                                    "Project name, path, and languages are required");
        }
        return m_repository->createProject(request, context);
    }

private:
    IProjectRepository* m_repository;
};

class OpenProjectUseCase {
public:
    explicit OpenProjectUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<OpenProjectResult>> execute(const OpenProjectRequest& request,
                                                             const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<OpenProjectResult>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<OpenProjectResult>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (request.path.trimmed().isEmpty()) {
            return detail::invalidProjectRequest<OpenProjectResult>("input.empty_path", "Project path is required");
        }
        return m_repository->openProject(request, context);
    }

private:
    IProjectRepository* m_repository;
};

class SaveProjectUseCase {
public:
    explicit SaveProjectUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<void>> execute(const SaveProjectRequest& request, const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<void>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (!request.project.isValid()) {
            return detail::invalidProjectRequest<void>("input.invalid_project", "Project data is invalid");
        }
        return m_repository->saveProject(request, context);
    }

private:
    IProjectRepository* m_repository;
};

class SaveProjectAsUseCase {
public:
    explicit SaveProjectAsUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<ProjectDescriptor>> execute(const SaveAsProjectRequest& request,
                                                             const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<ProjectDescriptor>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<ProjectDescriptor>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (!request.project.isValid() || request.newPath.trimmed().isEmpty()) {
            return detail::invalidProjectRequest<ProjectDescriptor>(
                "input.invalid_save_as", "Valid project data and a destination are required");
        }
        return m_repository->saveProjectAs(request, context);
    }

private:
    IProjectRepository* m_repository;
};

class CloseProjectUseCase {
public:
    explicit CloseProjectUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<void>> execute(const CloseProjectRequest& request, const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<void>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (!request.projectId.isValid()) {
            return detail::invalidProjectRequest<void>("input.invalid_project_id", "Project ID is invalid");
        }
        return m_repository->closeProject(request, context);
    }

private:
    IProjectRepository* m_repository;
};

class RecoverProjectUseCase {
public:
    explicit RecoverProjectUseCase(IProjectRepository* repository) : m_repository(repository) {}

    [[nodiscard]] QFuture<Result<OpenProjectResult>> execute(const OpenProjectRequest& request,
                                                             const OperationContext& context) {
        if (m_repository == nullptr) {
            return makeFailedFuture<OpenProjectResult>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_repository", "Project storage is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<OpenProjectResult>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Operation was cancelled"));
        }
        if (request.path.trimmed().isEmpty()) {
            return detail::invalidProjectRequest<OpenProjectResult>("input.empty_path", "Project path is required");
        }
        return m_repository->recoverProject(request, context);
    }

private:
    IProjectRepository* m_repository;
};

} // namespace manga::application
