#pragma once

#include "application/dto/PageInpaintDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IPageInpainter.h"

#include <QFuture>
#include <QImage>
#include <QString>

namespace manga::application {

struct ManualPageInpaintRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    QImage pageImage;
    QImage mask;
    QRect dirtyBounds;
    QString projectRoot;
    quint64 expectedSessionGeneration{0};
    quint64 currentSessionGeneration{0};
    QSize expectedPageSize;
    QString expectedClearLayerRef;
    OperationContext context;
};

struct PreparedPageInpaintChange {
    domain::ProjectId projectId;
    domain::PageId pageId;
    QString beforeRef;
    QString afterRef;
    quint64 expectedSessionGeneration{0};
    QSize expectedPageSize;
};

/// Performs one page-level stroke without changing the domain aggregate. The returned
/// immutable asset is adopted by PageClearLayerCommand only after the caller's stale guards pass.
class ManualInpaintWorkflow {
public:
    ManualInpaintWorkflow(IDerivedAssetStore* store, IPageInpainter* inpainter)
        : m_store(store), m_inpainter(inpainter) {}

    [[nodiscard]] QFuture<Result<PreparedPageInpaintChange>> execute(ManualPageInpaintRequest request);

private:
    IDerivedAssetStore* m_store{nullptr};
    IPageInpainter* m_inpainter{nullptr};
};

} // namespace manga::application
