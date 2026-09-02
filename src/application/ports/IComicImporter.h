#pragma once

#include "application/dto/ImportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Brings source images into an open project and hands the caller a batch capability that decides
/// whether the written assets are later discarded or kept. The batch record lives in the importer
/// instance, so a caller must finish with a batch through the same importer, while the marker the
/// importer leaves in the project lets a later openProject reclaim an interrupted batch.
class IComicImporter {
public:
    virtual ~IComicImporter() = default;

    /// Copies and normalizes every request source into the project originals and returns the pages
    /// they produced. The session for the request project and root must be open, otherwise
    /// project.not_open, and a source reached both directly and through a folder expands to a
    /// rejected whole request with input.duplicate_import_path before any disk work. All files share
    /// one project write section, so a concurrent save waits for this call on the worker. A stop
    /// requested before the work starts or observed between files deletes every asset this call
    /// committed and reports cancelled.import, so a cancelled import never reports partial success.
    /// Per file problems do not fail the call, they arrive in the result failures list keyed by
    /// source path, including import.unsupported_format. The result batch identifier is the context
    /// operation identifier when that is valid and a fresh identifier otherwise, and reusing an
    /// identifier replaces the assets the importer remembers for it. The work runs on a storage
    /// worker thread and fulfills the returned future there, so a continuation inherits that
    /// affinity and marshals to the GUI thread itself. Request and context are taken by value, and
    /// the importer must outlive the returned future.
    virtual QFuture<Result<ImportImagesResult>> importImages(ImportImagesRequest request, OperationContext context) = 0;

    /// Deletes the given assets, accepting only paths the importer recorded for request.batchId under
    /// the same project and root, so a caller cannot reach an arbitrary file. Any path outside that
    /// recorded set reports input.invalid_import_cleanup and deletes nothing, a path already gone is
    /// not an error, and a batch whose recorded set becomes empty is consumed so a later call for it
    /// also reports input.invalid_import_cleanup. A stop before the work starts reports
    /// cancelled.import and a closed session reports project.not_open. The work runs on a storage
    /// worker thread and fulfills the returned future there, and the importer must outlive that
    /// future while request and context are taken by value.
    virtual QFuture<Result<void>> discardImportedAssets(DiscardImportedAssetsRequest request,
                                                        OperationContext context) = 0;

    /// Ends the batch without deleting anything, which is how a caller states that the pages it
    /// adopted are now owned by the project. The batch is consumed, so a later discard or
    /// acknowledgement for that identifier reports input.invalid_import_cleanup, and the assets are
    /// no longer treated as orphans by a later project open. A stop before the work starts reports
    /// cancelled.import and a closed session reports project.not_open. The work runs on a storage
    /// worker thread and fulfills the returned future there, and the importer must outlive that
    /// future while request and context are taken by value.
    virtual QFuture<Result<void>> acknowledgeImportedAssets(AcknowledgeImportedAssetsRequest request,
                                                            OperationContext context) = 0;
};

} // namespace manga::application
