#pragma once

#include "application/dto/ProjectDto.h"
#include "application/error/ApplicationError.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <optional>
#include <string>

namespace manga::data {
class ProjectIoCoordinator;
class FileSystemProjectRepository;
} // namespace manga::data

class FileSystemProjectRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createProjectProducesLayout();
    void createProjectRejectsNonEmptyDirectory();
    void saveReopenRoundTripPreservesEdits();
    void secondSessionLockRejectsOpen();
    void staleLockIsCleared();
    void saveConflictDetectsExternalModification();
    void corruptCurrentRecoversFromBackup();
    void missingOriginalAssetRejectsOpen();
    void pathEscapeAssetRejectsOpen();
    void absolutePathAssetRejectsOpen();
    void derivedAssetMissingProducesWarning();
    void derivedAssetCorruptionProducesWarning();
    void saveAsRelocatesSessionAndKeepsOldDirectory();
    void saveAsRejectsNonEmptyTarget();
    void saveAsRejectsCurrentRootWithoutBlocking();
    void saveAsCommitFailureCleansExistingEmptyTarget();
    void repeatedOpenReturnsLatestSavedSnapshot();
    void closeReleasesLockAndSession();
    void closeWaitsForActivePageLease();
    void projectWriteOperationsSerialize();
    void recoveryPendingSessionCanCloseByRoot();
    void saveFailsWhenDiskSpaceProbeReportsFull();
    void saveRejectsWriteProtectedDirectory();
    void reparsePointEscapeRejectsOpen();
    void saveIgnoresStopRequestAfterTransactionStart();
    void saveResultDoesNotClearEditsMadeDuringSave();
    void errorsAndWarningsDoNotLeakPathsOrUserContent();
    void openNonExistentDirectoryReturnsNotFound();
    void openDirectoryWithoutProjectJsonReturnsNotAProject();
    void openProjectJsonPermissionDeniedReturnsPermissionDenied();
    void openWriteProtectedProjectReturnsReadOnly();
    void projectLockErrorMapping();

private:
    [[nodiscard]] QString newRoot();
    [[nodiscard]] std::optional<manga::application::ProjectDescriptor> createProject(const QString& path);
    [[nodiscard]] std::optional<manga::application::OpenProjectResult> openProject(const QString& path);
    [[nodiscard]] manga::application::ApplicationError openProjectError(const QString& path);
    [[nodiscard]] manga::application::ApplicationError saveProject(const manga::domain::ComicProject& project);
    [[nodiscard]] manga::application::ApplicationError saveProjectAs(const manga::domain::ComicProject& project,
                                                                     const QString& newPath);
    [[nodiscard]] manga::application::ApplicationError closeProject(const manga::domain::ComicProject& project);

    QTemporaryDir m_tempDir;
    std::unique_ptr<manga::data::ProjectIoCoordinator> m_coordinator;
    std::unique_ptr<manga::data::FileSystemProjectRepository> m_repository;
    int m_rootCounter{0};
    std::optional<manga::application::ProjectDescriptor> m_saveAsDescriptor;
};
