#pragma once

#include "application/error/ApplicationError.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/ReadingDirection.h"
#include "domain/model/Uuid.h"

#include <QFlags>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace manga::application {

/// Processing stages that can be enabled or combined for batch execution.
enum class BatchStageFlag : quint32 {
    Detect = 1 << 0,
    Ocr = 1 << 1,
    Translate = 1 << 2,
    Clear = 1 << 3,
    Layout = 1 << 4,
    All = Detect | Ocr | Translate | Clear | Layout,
};
Q_DECLARE_FLAGS(BatchStageFlags, BatchStageFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(BatchStageFlags)

/// Scope determining which pages in the project are targeted for batch processing.
enum class BatchScopeType {
    AllPages,
    SelectedPages,
    Unprocessed,
    FailedOnly,
};

using ReplacePolicy = ReplaceManualPolicy;

/// Request configuration for a multi-page batch processing run.
struct BatchProcessingRequest {
    domain::ProjectId projectId;
    QVector<domain::PageId> pageIds;
    BatchStageFlags stages{BatchStageFlag::All};
    BatchScopeType scope{BatchScopeType::AllPages};
    ReplaceManualPolicy replacePolicy{ReplaceManualPolicy::Keep};

    domain::ComicProject* project{nullptr};
    QString projectRoot;
    QString sourceLanguage;
    QString targetLanguage;
    domain::ReadingDirection readingDirection{domain::ReadingDirection::RightToLeft};
    double minConfidence{0.25};

    friend bool operator==(const BatchProcessingRequest&, const BatchProcessingRequest&) = default;
};

/// Per-page processing summary entry in the final batch report.
struct BatchPageSummary {
    domain::PageId pageId;
    bool success{false};
    QString failureReason;

    friend bool operator==(const BatchPageSummary&, const BatchPageSummary&) = default;
};

/// Summary report emitted when a multi-page batch processing run concludes.
struct BatchProcessingReport {
    domain::ProjectId projectId;
    int totalPages{0};
    int completedPages{0};
    int failedPages{0};
    QVector<BatchPageSummary> pageSummaries;

    /// Project metadata persisted once at the end of the run. The revision lets the editor clear
    /// its dirty state only when nothing changed after the write.
    bool projectSaved{false};
    quint64 savedRevision{0};

    friend bool operator==(const BatchProcessingReport&, const BatchProcessingReport&) = default;
};

} // namespace manga::application

Q_DECLARE_METATYPE(manga::application::BatchStageFlag)
Q_DECLARE_METATYPE(manga::application::BatchStageFlags)
Q_DECLARE_METATYPE(manga::application::BatchScopeType)
Q_DECLARE_METATYPE(manga::application::BatchProcessingRequest)
Q_DECLARE_METATYPE(manga::application::BatchPageSummary)
Q_DECLARE_METATYPE(manga::application::BatchProcessingReport)
