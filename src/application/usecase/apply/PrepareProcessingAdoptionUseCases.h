#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/operation/OperationStage.h"
#include "application/usecase/apply/ApplyUseCases.h"         // For PreparedClearChange
#include "application/workflow/SmartTranslatePageWorkflow.h" // For ProcessingIssue
#include "domain/model/ComicProject.h"

#include <QFuture>
#include <QVector>

#include <optional>

namespace manga::application {

enum class ReplaceManualPolicy {
    Keep,
    Replace
};

struct PreparedRegionChange {
    domain::PageId pageId;
    domain::RegionId regionId;
    quint64 expectedRevision{0};
    domain::PageRegionEdit before;
    domain::PageRegionEdit after;
};

struct PreparedProcessingAdoption {
    OperationStage stage;
    QVector<PreparedRegionChange> changes;
    QVector<ProcessingIssue> skipped;
    std::optional<PreparedClearChange> clearAssetChange;
};

struct PrepareProcessingAdoptionRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    OperationStage stage;
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};

    // The batch payload
    QVector<OcrResponse> ocr;
    std::optional<TranslationResponse> translation;
    QVector<ClearResponse> clear;
    QVector<LayoutResponse> layout;

    QHash<domain::RegionId, quint64> expectedRevisions;

    // Optional input properties
    QSize pageSize;      // normalized working image size, used for a fresh transparent layer (Clear stage)
    QString projectRoot; // runtime absolute path, never persisted (Clear stage)
    OperationContext context;

    ReplaceManualPolicy replaceManualPolicy{ReplaceManualPolicy::Keep};
};

class PrepareProcessingAdoptionUseCase {
public:
    PrepareProcessingAdoptionUseCase(domain::ComicProject* project, PrepareClearResultUseCase* prepareClearUseCase);

    [[nodiscard]] QFuture<Result<PreparedProcessingAdoption>> execute(const PrepareProcessingAdoptionRequest& request);

private:
    domain::ComicProject* m_project;
    PrepareClearResultUseCase* m_prepareClearUseCase;
};

} // namespace manga::application
