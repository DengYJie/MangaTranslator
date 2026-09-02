#include "QSettingsRecentProjectsStore.h"

#include "data/DataAsync.h"
#include "data/image/ImageNormalization.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectLayout.h"
#include "data/project/ProjectSerializer.h"

#include <QFile>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace manga::data {

namespace {

// Persisted state. The list lives in the QSettings array group named recent_projects, written under
// the application and organization names set at startup, and every stored entry carries the name,
// path and lastOpenedTimeMs child keys used below. Renaming any of them orphans the values already
// on disk instead of migrating them. An entry is identified by its path, not by its name.
const QString kRecentProjectsKey = QStringLiteral("recent_projects");
} // namespace

QVector<application::RecentProject> QSettingsRecentProjectsStore::loadRecentProjects() const {
    QSettings settings;
    QVector<application::RecentProject> projects;

    int size = settings.beginReadArray(kRecentProjectsKey);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        application::RecentProject p;
        p.name = settings.value(QStringLiteral("name")).toString();
        p.path = settings.value(QStringLiteral("path")).toString();
        p.lastOpenedTimeMs = settings.value(QStringLiteral("lastOpenedTimeMs")).toLongLong();
        projects.append(p);
    }
    settings.endArray();

    // Newest first. addRecentProject truncates from the tail, so this sort is what makes the
    // dropped entries the least recently opened ones.
    std::sort(projects.begin(), projects.end(), [](const auto& a, const auto& b) {
        return a.lastOpenedTimeMs > b.lastOpenedTimeMs;
    });

    return projects;
}

void QSettingsRecentProjectsStore::addRecentProject(const application::RecentProject& project) {
    auto projects = loadRecentProjects();

    auto it = std::find_if(projects.begin(), projects.end(), [&](const auto& p) { return p.path == project.path; });

    if (it != projects.end()) {
        it->lastOpenedTimeMs = project.lastOpenedTimeMs;
        it->name = project.name;
    } else {
        projects.prepend(project);
    }

    // One cap shared by the comparison and the resize, so the stored group never grows past it and
    // both bounds have to move together.
    if (projects.size() > 20) {
        projects.resize(20);
    }

    QSettings settings;
    settings.beginWriteArray(kRecentProjectsKey);
    for (int i = 0; i < projects.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), projects[i].name);
        settings.setValue(QStringLiteral("path"), projects[i].path);
        settings.setValue(QStringLiteral("lastOpenedTimeMs"), projects[i].lastOpenedTimeMs);
    }
    settings.endArray();
}

void QSettingsRecentProjectsStore::removeRecentProject(const QString& path) {
    auto projects = loadRecentProjects();

    projects.erase(std::remove_if(projects.begin(), projects.end(), [&](const auto& p) { return p.path == path; }),
                   projects.end());

    QSettings settings;
    settings.beginWriteArray(kRecentProjectsKey);
    for (int i = 0; i < projects.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), projects[i].name);
        settings.setValue(QStringLiteral("path"), projects[i].path);
        settings.setValue(QStringLiteral("lastOpenedTimeMs"), projects[i].lastOpenedTimeMs);
    }
    settings.endArray();
}

void QSettingsRecentProjectsStore::clearRecentProjects() {
    QSettings settings;
    settings.remove(kRecentProjectsKey);
}

QFuture<application::Result<QImage>>
QSettingsRecentProjectsStore::loadProjectCover(QString path, application::OperationContext context) {
    return runDataOperation<QImage>([path = std::move(path),
                                     context = std::move(context)]() -> application::Result<QImage> {
        const auto cancelled = [] {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.user_requested"),
                                                                     QStringLiteral("Cover loading was cancelled")));
        };
        if (context.stopToken.stop_requested()) {
            return cancelled();
        }
        const QString root = canonicalDirectoryPath(path);
        if (root.isEmpty()) {
            return QImage{};
        }
        QFile projectFile(resolveInsideCanonicalRoot(root, QString(kProjectFileName)));
        constexpr qint64 kMaxProjectBytes = 64 * 1024 * 1024;
        if (!projectFile.open(QIODevice::ReadOnly) || projectFile.size() > kMaxProjectBytes) {
            return QImage{};
        }
        const auto bytes = projectFile.read(kMaxProjectBytes + 1);
        if (bytes.size() > kMaxProjectBytes) {
            return QImage{};
        }
        const auto project = ProjectSerializer::deserialize(bytes);
        if (!project || project->pages().empty()) {
            return QImage{};
        }
        const auto first =
            std::min_element(project->pages().cbegin(), project->pages().cend(), [](const auto& a, const auto& b) {
                return a.orderIndex() < b.orderIndex();
            });
        const QString thumbnail =
            QStringLiteral("assets/derived/%1/thumbnail/v1.png").arg(QString::fromStdString(first->id().toString()));
        const QStringList candidates{thumbnail, QString::fromStdString(first->originalAssetPath())};
        for (const auto& relativePath : candidates) {
            if (context.stopToken.stop_requested()) {
                return cancelled();
            }
            const auto imagePath = resolveInsideCanonicalRoot(root, relativePath);
            if (imagePath.isEmpty()) {
                continue;
            }
            auto decoded = loadNormalizedImage(imagePath, context.stopToken);
            if (context.stopToken.stop_requested()) {
                return cancelled();
            }
            if (decoded && !decoded->image.isNull()) {
                auto cover = decoded->image.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                if (context.stopToken.stop_requested()) {
                    return cancelled();
                }
                return cover;
            }
        }
        return QImage{};
    });
}

} // namespace manga::data
