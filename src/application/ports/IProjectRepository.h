#pragma once

#include "application/dto/ProjectDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Owns the on-disk project sessions that the application opens, saves, and closes.
class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;

    /// Creates the project layout at request.path and registers its session. The path must be
    /// absolute, and an existing target must be an empty directory. A stop requested before the
    /// work starts reports cancelled.project_operation, and a created layout is rolled back when
    /// the setup fails. A bad path reports input.invalid_project, a non-empty target reports
    /// persistence.target_not_empty, and a root another holder owns reports persistence.locked.
    /// The work and the fulfillment of the returned future both run on a storage worker thread, so
    /// a continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the repository must outlive the returned future.
    virtual QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest request,
                                                             OperationContext context) = 0;

    /// Loads the project at request.path and registers its session, or returns the session this
    /// process already holds for that root, which makes a repeated open idempotent. A stop before
    /// the work starts reports cancelled.project_operation. A directory without a project file
    /// reports project.not_a_project, an unreadable path reports persistence.not_found, a lock held
    /// elsewhere reports persistence.locked, and undecodable data reports persistence.recovery_failed
    /// after registering a session that only recoverProject can serve. Per asset problems arrive as
    /// warnings on the result rather than as a failure, and a successful open deletes original
    /// images that an interrupted import left unreferenced. The work and the fulfillment of the
    /// returned future both run on a storage worker thread, so a continuation inherits that affinity.
    /// The request and context are taken by value, while the repository must outlive the future.
    virtual QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest request, OperationContext context) = 0;

    /// Loads the backup copy of a session that openProject left recovery pending and marks it
    /// recovered, without rewriting the stored project file. A stop before the work starts reports
    /// cancelled.project_operation. A session with nothing to recover, an unreadable backup, and an
    /// undecodable backup all report persistence.recovery_failed, and validated asset problems are
    /// reported as project category codes. The result carries recoverySource Backup plus a
    /// project.recovered_from_backup warning. The work and the fulfillment of the returned future
    /// both run on a storage worker thread, so a continuation inherits that affinity. The request
    /// and context are taken by value, while the repository must outlive the future.
    virtual QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest request, OperationContext context) = 0;

    /// Writes request.project as the stored snapshot of the session holding that project id and
    /// returns the revision it committed. A stop is honored only at the entry checkpoint and at the
    /// last checkpoint before the transaction, which then runs to completion so a begun save is
    /// never interrupted. A missing or recovery pending session reports project.not_open, a project
    /// file that changed outside this session reports persistence.save_conflict, an unwilling volume
    /// reports persistence.read_only or persistence.disk_full, and a copy that could not be rotated
    /// reports persistence.backup_failed. After a successful save the repository discards derived
    /// assets the snapshot no longer references from a separate worker and ignores the outcome of
    /// that cleanup, so the result says nothing about it. The work and the fulfillment of the
    /// returned future both run on a storage worker thread, so a continuation inherits that
    /// affinity. The request and context are taken by value, while the repository must outlive the
    /// returned future.
    virtual QFuture<Result<SaveProjectResult>> saveProject(SaveProjectRequest request, OperationContext context) = 0;

    /// Copies the project to request.newPath through a staging tree and moves the session onto the
    /// new root, returning the new descriptor and the committed revision. A stop is honored during
    /// the staged copy, which then removes what it wrote and leaves the session on its original
    /// root, while the final move is not cancelable. An unopened or recovery pending session
    /// reports project.not_open, a bad target reports input.invalid_save_as, an occupied target
    /// reports persistence.target_not_empty or persistence.locked, and an interrupted move reports
    /// persistence.atomic_commit_failed. The work and the fulfillment of the returned future both
    /// run on a storage worker thread, so a continuation inherits that affinity. The request and
    /// context are taken by value, while the repository must outlive the future.
    virtual QFuture<Result<SaveAsProjectResult>> saveProjectAs(SaveAsProjectRequest request,
                                                               OperationContext context) = 0;

    /// Ends the session selected by request.projectId, falling back to request.projectRoot so a
    /// recovery failure that has no valid id can still release its root. It releases the lock file
    /// and drops runtime state without writing project data. A stop before the work starts reports
    /// cancelled.project_operation, an unknown selection reports project.not_open, and a root whose
    /// in-flight asset reads did not finish inside the adapter wait period reports
    /// project.close_timeout as a retryable error with the session left open. The work and the
    /// fulfillment of the returned future both run on a storage worker thread, so a continuation
    /// inherits that affinity. The request and context are taken by value, while the repository must
    /// outlive the returned future.
    virtual QFuture<Result<void>> closeProject(CloseProjectRequest request, OperationContext context) = 0;
};

} // namespace manga::application
