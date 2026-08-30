#pragma once

#include "domain/model/ComicProject.h"

#include <QString>
#include <QVector>

namespace manga::application {

struct ProjectDto {
    QString id;
    QString name;
};

enum class RecoverySource {
    Current,
    Backup
};

struct ProjectWarning {
    QString code;
    QString safeMessage;
    QString path;
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
    QVector<ProjectWarning> warnings;
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

// Identifies the exact aggregate snapshot committed by the repository.
struct SaveProjectResult {
    quint64 savedRevision{0};
};

struct SaveAsProjectRequest {
    domain::ComicProject project;
    QString newPath;
};

struct SaveAsProjectResult {
    ProjectDescriptor descriptor;
    quint64 savedRevision{0};
};

struct CloseProjectRequest {
    domain::ProjectId projectId;
    // Recovery failures can own a root lock before a valid project identifier is available.
    QString projectRoot;
};

/// Flat, view-facing descriptor of one page in an open project session.
struct PageDescriptor {
    domain::PageId id;
    int orderIndex{0};
    domain::ImageSize imageSize;
    QString originalAssetPath; // Project-relative, e.g. assets/original/page_0001.png
    int regionCount{0};
};

/// Everything the editor skeleton needs to present an opened or freshly imported project
/// without holding domain pointers. Carries open/import warnings for later surfacing.
struct ProjectSessionDescriptor {
    domain::ProjectId projectId;
    QString projectRootPath;
    QString projectName;
    QVector<PageDescriptor> pages;
    QVector<ProjectWarning> warnings;
};

} // namespace manga::application

Q_DECLARE_METATYPE(manga::application::PageDescriptor)
Q_DECLARE_METATYPE(manga::application::ProjectSessionDescriptor)
