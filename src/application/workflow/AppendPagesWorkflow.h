#pragma once

#include "application/dto/ImportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"
#include "domain/model/ComicProject.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace manga::application {

class IComicImporter;
class IProjectRepository;

/// Request to append image pages to an already open project aggregate.
struct AppendPagesRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    QVector<QString> filePaths;
    /// Open aggregate mutated on the GUI thread after the import finishes. Non-owning.
    domain::ComicProject* project{nullptr};
    quint64 sessionGeneration{0};
    /// Reads the live session generation on the GUI thread at apply time. When unset,
    /// sessionGeneration is used for both sides of the stale-result check.
    std::function<quint64()> currentSessionGeneration;
};

/// Outcome of one append run. projectSaved and savedRevision describe the project
/// metadata save that persists the appended pages.
struct AppendPagesReport {
    QVector<domain::PageId> pageIds;
    int failedSources{0};
    bool projectSaved{false};
    quint64 savedRevision{0};
};

/// Appends image pages to an open project: imports the assets, applies the new
/// pages to the aggregate on the GUI thread, acknowledges the asset batch, and
/// saves the project metadata.
class AppendPagesWorkflow : public QObject {
    Q_OBJECT

public:
    AppendPagesWorkflow(IComicImporter* importer, IProjectRepository* projectRepository, QObject* parent = nullptr);

    /// Runs one append pipeline. Call on the GUI thread. The returned future resolves
    /// after the final save step, or with the first stage error. Cancellation through
    /// the context stop token is observed before the aggregate mutation. Once the
    /// pages are applied, the acknowledge and save stages run to completion so the
    /// adopted assets never dangle.
    [[nodiscard]] QFuture<Result<AppendPagesReport>> execute(AppendPagesRequest request, OperationContext context);

private:
    IComicImporter* m_importer{nullptr};
    IProjectRepository* m_projectRepository{nullptr};
};

} // namespace manga::application
