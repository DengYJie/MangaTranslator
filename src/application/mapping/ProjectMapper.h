#pragma once

#include "application/dto/ProjectDto.h"
#include "domain/model/ComicProject.h"

namespace manga::application::mapping {

class ProjectMapper {
public:
    static ProjectDto toDto(const domain::ComicProject& project) {
        ProjectDto dto;
        dto.id = QString::fromStdString(project.id().toString());
        dto.name = QString::fromStdString(project.name());
        return dto;
    }

    static ProjectSessionDescriptor toSessionDescriptor(const domain::ComicProject& project,
                                                        const QString& projectRootPath) {
        ProjectSessionDescriptor descriptor;
        descriptor.projectId = project.id();
        descriptor.projectRootPath = projectRootPath;
        descriptor.projectName = QString::fromStdString(project.name());
        descriptor.pages.reserve(static_cast<qsizetype>(project.pages().size()));
        for (const auto& page : project.pages()) {
            PageDescriptor entry;
            entry.id = page.id();
            entry.orderIndex = page.orderIndex();
            entry.imageSize = page.imageSize();
            entry.originalAssetPath = QString::fromStdString(page.originalAssetPath());
            entry.regionCount = static_cast<int>(page.regions().size());
            descriptor.pages.append(entry);
        }
        return descriptor;
    }

    static ProjectSessionDescriptor toSessionDescriptor(const OpenedProjectSession& session) {
        return toSessionDescriptor(session.project, session.projectRootPath);
    }
};

} // namespace manga::application::mapping
