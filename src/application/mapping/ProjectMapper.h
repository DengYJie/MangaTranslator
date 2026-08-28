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
};

} // namespace manga::application::mapping
