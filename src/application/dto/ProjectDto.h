#pragma once

#include "domain/model/ComicProject.h"

#include <QString>

namespace manga::application {

struct ProjectDto {
    QString id;
    QString name;
};

enum class RecoverySource {
    Current,
    Backup
};

struct ProjectDescriptor {
    domain::ProjectId id;
    QString name;
    QString path;
};

struct OpenProjectRequest {
    QString path;
};

struct OpenProjectResult {
    domain::ComicProject project;
    RecoverySource recoverySource{RecoverySource::Current};
};

struct CreateProjectRequest {
    QString name;
    QString path;
    domain::LanguageCode sourceLanguage;
    domain::LanguageCode targetLanguage;
    domain::ReadingDirection readingDirection;
};

struct SaveProjectRequest {
    domain::ComicProject project;
};

struct SaveAsProjectRequest {
    domain::ComicProject project;
    QString newPath;
};

struct CloseProjectRequest {
    domain::ProjectId projectId;
};

} // namespace manga::application
