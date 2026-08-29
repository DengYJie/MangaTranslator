#pragma once

#include "application/ports/IProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QFuture>
#include <QMutex>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace manga::data {

struct RepositorySession;

/// Returns the bytes still available on the volume holding the project root; a negative
/// result means "unknown" and never blocks. Production uses QStorageInfo; tests inject
/// probes that simulate a full disk.
using DiskSpaceProbe = std::function<qint64(const QString& projectRoot)>;

/// Filesystem-backed project repository using the directory layout project.json plus
/// assets/original and assets/derived. Saves are atomic QSaveFile commits guarded by a
/// fingerprint check with a single-generation project.json.bak backup; sessions are held by
/// an inter-process QLockFile with stale-lock recovery.
class FileSystemProjectRepository final : public application::IProjectRepository {
public:
    explicit FileSystemProjectRepository(ProjectIoCoordinator* coordinator, DiskSpaceProbe probe = {});
    ~FileSystemProjectRepository() override;

    void setDiskSpaceProbe(DiskSpaceProbe probe);

    [[nodiscard]] QFuture<application::Result<application::ProjectDescriptor>>
    createProject(application::CreateProjectRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::OpenProjectResult>>
    openProject(application::OpenProjectRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::OpenProjectResult>>
    recoverProject(application::OpenProjectRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<void>> saveProject(application::SaveProjectRequest request,
                                                                 application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::ProjectDescriptor>>
    saveProjectAs(application::SaveAsProjectRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<void>> closeProject(application::CloseProjectRequest request,
                                                                  application::OperationContext context) override;

private:
    [[nodiscard]] std::shared_ptr<RepositorySession> findSession(const domain::ProjectId& projectId);
    [[nodiscard]] std::shared_ptr<RepositorySession> findSessionByRoot(const QString& canonicalRoot);
    void registerSession(const std::shared_ptr<RepositorySession>& session);
    void forgetSession(const QString& canonicalRoot);
    [[nodiscard]] bool hasDiskSpace(const QString& root, qint64 requiredBytes) const;

    ProjectIoCoordinator* m_coordinator;
    DiskSpaceProbe m_probe;
    QMutex m_mutex;
    std::map<QString, std::shared_ptr<RepositorySession>> m_sessions;
};

} // namespace manga::data
