#pragma once

#include "application/dto/BatchProcessingDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"
#include "application/operation/OperationContext.h"
#include "application/operation/OperationId.h"
#include "application/ports/ITextDetector.h"
#include "domain/model/ComicProject.h"

#include <QFuture>
#include <QObject>

namespace manga::application {

class RecognizeRegionUseCase;
class TranslatePageUseCase;
class ClearRegionUseCase;
class LayoutRegionUseCase;
class IPageImageLoader;
class IDerivedAssetStore;
class IProjectRepository;
class OperationRegistry;

using IDetector = ITextDetector;

/// Multi-page batch processing workflow executing Detect, OCR, Translate, Clear, and Layout
/// sequentially across selected pages, automatically saving and committing each page.
class BatchPagesProcessingWorkflow : public QObject {
    Q_OBJECT

public:
    BatchPagesProcessingWorkflow(RecognizeRegionUseCase* ocrUseCase,
                                 TranslatePageUseCase* translateUseCase,
                                 ClearRegionUseCase* clearUseCase,
                                 LayoutRegionUseCase* layoutUseCase,
                                 ITextDetector* detector,
                                 IPageImageLoader* pageImageLoader,
                                 IDerivedAssetStore* derivedAssetStore,
                                 IProjectRepository* projectRepository,
                                 OperationRegistry* registry,
                                 QObject* parent = nullptr);
    ~BatchPagesProcessingWorkflow() override;

    [[nodiscard]] QFuture<Result<BatchProcessingReport>>
    execute(BatchProcessingRequest request, OperationContext context, quint64 sessionGeneration);

    void cancel(OperationId id);
    void setTextDetector(ITextDetector* detector);

Q_SIGNALS:
    void pageCommitted(domain::PageId pageId);

private:
    RecognizeRegionUseCase* m_ocrUseCase{nullptr};
    TranslatePageUseCase* m_translateUseCase{nullptr};
    ClearRegionUseCase* m_clearUseCase{nullptr};
    LayoutRegionUseCase* m_layoutUseCase{nullptr};
    ITextDetector* m_detector{nullptr};
    IPageImageLoader* m_pageImageLoader{nullptr};
    IDerivedAssetStore* m_derivedAssetStore{nullptr};
    IProjectRepository* m_projectRepository{nullptr};
    OperationRegistry* m_registry{nullptr};
};

} // namespace manga::application
