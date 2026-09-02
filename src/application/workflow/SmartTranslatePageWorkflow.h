#pragma once

#include "application/dto/ClearDto.h"
#include "application/dto/LayoutDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"
#include "application/operation/OperationId.h"
#include "application/operation/OperationStage.h"
#include "domain/model/ComicProject.h"

#include <QFuture>
#include <QImage>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::application {

class RecognizeRegionUseCase;
class TranslatePageUseCase;
class ClearRegionUseCase;
class LayoutRegionUseCase;
class OperationRegistry;

struct RegionProcessingTarget {
    domain::RegionId regionId;
    domain::RectGeometry geometry;
    std::optional<domain::RectGeometry> bubbleGeometry;
    std::optional<domain::PolygonGeometry> bubblePolygon;
    QString sourceText;
    QString effectiveTranslation;
    quint64 contentRevision{0};
    bool hasManualTranslation{false};
    bool hasManualSource{false};
};

struct ProcessingIssue {
    std::optional<domain::RegionId> regionId;
    QString stage;
    ApplicationError error;
};

struct PageProcessingProposal {
    domain::ProjectId projectId;
    domain::PageId pageId;
    OperationId rootOperationId;
    QVector<OcrResponse> ocr;
    std::optional<TranslationResponse> translation;
    QVector<ClearResponse> clear;
    QVector<LayoutResponse> layout;
    QVector<ProcessingIssue> issues;
};

class SmartTranslatePageWorkflow : public QObject {
    Q_OBJECT
public:
    SmartTranslatePageWorkflow(RecognizeRegionUseCase* ocrUseCase,
                               TranslatePageUseCase* translateUseCase,
                               ClearRegionUseCase* clearUseCase,
                               LayoutRegionUseCase* layoutUseCase,
                               OperationRegistry* registry,
                               QObject* parent = nullptr);
    ~SmartTranslatePageWorkflow() override;

    /// Calls and continuations run on the application thread. When outContext is supplied,
    /// the caller retains it and settles successful proposals after adoption completes.
    [[nodiscard]] QFuture<Result<PageProcessingProposal>>
    execute(domain::ProjectId projectId,
            domain::PageId pageId,
            QVector<RegionProcessingTarget> targets,
            QString sourceLanguage,
            QString targetLanguage,
            QImage pageImage,
            quint64 sessionGeneration,
            OperationId& outOperationId,
            domain::ReadingDirection readingDirection = domain::ReadingDirection::RightToLeft,
            OperationContext* outContext = nullptr);

    void cancel(OperationId id);

private:
    RecognizeRegionUseCase* m_ocrUseCase;
    TranslatePageUseCase* m_translateUseCase;
    ClearRegionUseCase* m_clearUseCase;
    LayoutRegionUseCase* m_layoutUseCase;
    OperationRegistry* m_registry;
};

} // namespace manga::application
