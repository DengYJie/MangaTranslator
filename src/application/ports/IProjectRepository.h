#pragma once

#include "application/dto/ProjectDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;

    virtual QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest request,
                                                             OperationContext context) = 0;
    virtual QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest request, OperationContext context) = 0;
    virtual QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest request, OperationContext context) = 0;
    virtual QFuture<Result<SaveProjectResult>> saveProject(SaveProjectRequest request, OperationContext context) = 0;
    virtual QFuture<Result<SaveAsProjectResult>> saveProjectAs(SaveAsProjectRequest request,
                                                               OperationContext context) = 0;
    virtual QFuture<Result<void>> closeProject(CloseProjectRequest request, OperationContext context) = 0;
};

} // namespace manga::application
