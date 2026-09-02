#pragma once

#include "application/dto/ClearDto.h"
#include "application/dto/LayoutDto.h"
#include "application/dto/ManualPaintDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/TranslationDto.h"
#include "application/operation/OperationId.h"
#include "application/operation/OperationStage.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/processing/ManualInpaintWorkflow.h"
#include "application/workflow/SmartTranslatePageWorkflow.h"
#include "domain/model/ComicProject.h"
#include "domain/model/Uuid.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <memory>
#include <optional>

namespace manga::application {
class RecognizeRegionUseCase;
class TranslatePageUseCase;
class ClearRegionUseCase;
class LayoutRegionUseCase;
class PrepareProcessingAdoptionUseCase;
class PrepareClearResultUseCase;
class OperationRegistry;
class IOcrEngine;
class ITranslator;
class IInpainter;
class IPageInpainter;
class ITextLayouter;
class IDerivedAssetStore;
class ITextDetector;
} // namespace manga::application

namespace manga::ui::screen::editor {

class EditorViewModel;

/// GUI-thread controller coordinating asynchronous page and region processing tasks
/// (OCR, Translate, Clear, Layout, SmartTranslate, Detect). Owns QFutureWatchers, handles
/// same-target cancellation, project close teardown, and proposal lifecycle.
class EditorProcessingController : public QObject {
    Q_OBJECT
public:
    EditorProcessingController(EditorViewModel* viewModel,
                               application::IOcrEngine* ocrEngine,
                               application::ITranslator* translator,
                               application::IInpainter* inpainter,
                               application::IPageInpainter* pageInpainter,
                               application::ITextLayouter* layouter,
                               application::IDerivedAssetStore* assetStore,
                               application::OperationRegistry* registry,
                               application::ITextDetector* textDetector = nullptr,
                               QObject* parent = nullptr);

    EditorProcessingController(EditorViewModel* viewModel,
                               application::RecognizeRegionUseCase* ocrUseCase,
                               application::TranslatePageUseCase* translateUseCase,
                               application::ClearRegionUseCase* clearUseCase,
                               application::LayoutRegionUseCase* layoutUseCase,
                               application::SmartTranslatePageWorkflow* smartWorkflow,
                               application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase,
                               application::OperationRegistry* registry,
                               QObject* parent = nullptr);

    ~EditorProcessingController() override;

    // Commands / User Intents
    void smartTranslatePage(const domain::PageId& pageId);
    void smartTranslateRegions(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void detectRegions(const domain::PageId& pageId);
    void recognizeSelectedRegions();
    void recognizeRegion(const domain::PageId& pageId, const domain::RegionId& regionId);
    void translatePage(const domain::PageId& pageId);
    void clearSelectedRegions();
    void clearRegion(const domain::PageId& pageId, const domain::RegionId& regionId);
    void inpaintStroke(const domain::PageId& pageId, const QVector<QPointF>& points, double diameter);
    void paintStroke(const domain::PageId& pageId,
                     const QVector<QPointF>& points,
                     application::ManualStrokeMode mode,
                     const QColor& color,
                     double diameter);
    void layoutSelectedRegions();
    void layoutRegion(const domain::PageId& pageId, const domain::RegionId& regionId);

    void cancelOperation(application::OperationId operationId);
    void cancelPageOperations(const domain::PageId& pageId);
    void cancelAll();

    void
    retryFailedStage(const domain::PageId& pageId, const domain::RegionId& regionId, application::OperationStage stage);

    void adoptProposal(const domain::PageId& pageId);
    void rejectProposal(const domain::PageId& pageId);

    void handlePageSwitched(const domain::PageId& newPageId);
    void reset();

    [[nodiscard]] EditorViewModel* viewModel() const {
        return m_viewModel;
    }
    void setViewModel(EditorViewModel* vm) {
        m_viewModel = vm;
    }
    /// Replaces the page inpainter while preserving old adapters for active workflows.
    void setPageInpainter(application::IPageInpainter* pageInpainter);
    /// Replaces the text detector while preserving old adapters for active workflows.
    void setTextDetector(application::ITextDetector* textDetector);

    [[nodiscard]] bool hasActiveOperation(const domain::PageId& pageId) const;
    [[nodiscard]] bool hasProposal(const domain::PageId& pageId) const;
    [[nodiscard]] const application::PageProcessingProposal* proposal(const domain::PageId& pageId) const;
    [[nodiscard]] const application::PreparedProcessingAdoption* preparedAdoption(const domain::PageId& pageId) const;

Q_SIGNALS:
    void processingStarted(const domain::PageId& pageId, application::OperationId operationId);
    void processingFinished(const domain::PageId& pageId, bool success);
    void proposalReady(const domain::PageId& pageId);
    void proposalAdopted(const domain::PageId& pageId);
    void proposalRejected(const domain::PageId& pageId);

private:
    struct WatcherKey {
        domain::ProjectId projectId;
        domain::PageId pageId;
        std::optional<domain::RegionId> regionId;
        application::OperationStage stage;

        bool operator==(const WatcherKey& other) const {
            return projectId == other.projectId && pageId == other.pageId && regionId == other.regionId &&
                   stage == other.stage;
        }
    };

    friend size_t qHash(const WatcherKey& key, size_t seed) noexcept {
        size_t h = ::qHash(QString::fromStdString(key.projectId.toString()), seed);
        h ^= ::qHash(QString::fromStdString(key.pageId.toString()), seed);
        if (key.regionId.has_value()) {
            h ^= ::qHash(QString::fromStdString(key.regionId->toString()), seed);
        }
        h ^= ::qHash(static_cast<int>(key.stage), seed);
        return h;
    }

    struct ActiveTask {
        application::OperationId operationId;
        quint64 sessionGeneration{0};
        WatcherKey key;
        QVector<domain::RegionId> targetRegionIds;
        QHash<domain::RegionId, quint64> expectedRevisions;
    };

    void cancelMatchingTask(const WatcherKey& key);
    void smartTranslate(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void updateProcessingStateForCurrentPage();
    void handleSmartTranslateFinished(const WatcherKey& key,
                                      application::OperationId operationId,
                                      quint64 sessionGen,
                                      QFutureWatcher<application::Result<application::PageProcessingProposal>>* watcher,
                                      QVector<domain::RegionId> targetRegions,
                                      QHash<domain::RegionId, quint64> expectedRevisions);
    void handleStageFinished(const WatcherKey& key,
                             application::OperationId operationId,
                             quint64 sessionGen,
                             application::OperationStage stage,
                             bool success,
                             const application::ApplicationError* error = nullptr);
    void prepareAdoptionAsync(const WatcherKey& key,
                              const application::PrepareProcessingAdoptionRequest& prepReq,
                              quint64 sessionGen);
    void handlePrepareAdoptionFinished(
        const WatcherKey& key,
        quint64 sessionGen,
        QFutureWatcher<application::Result<application::PreparedProcessingAdoption>>* watcher);

    EditorViewModel* m_viewModel{nullptr};                  // non-owning
    application::OperationRegistry* m_registry{nullptr};    // non-owning
    application::IDerivedAssetStore* m_assetStore{nullptr}; // non-owning
    application::IPageInpainter* m_pageInpainter{nullptr};  // non-owning
    application::ITextDetector* m_textDetector{nullptr};    // non-owning

    // Owned UseCases when created from ports
    std::unique_ptr<application::RecognizeRegionUseCase> m_ownedOcrUseCase;
    std::unique_ptr<application::TranslatePageUseCase> m_ownedTranslateUseCase;
    std::unique_ptr<application::ClearRegionUseCase> m_ownedClearUseCase;
    std::unique_ptr<application::LayoutRegionUseCase> m_ownedLayoutUseCase;
    std::unique_ptr<application::SmartTranslatePageWorkflow> m_ownedSmartWorkflow;
    std::unique_ptr<application::PrepareClearResultUseCase> m_ownedPrepareClearUseCase;
    std::unique_ptr<application::PrepareProcessingAdoptionUseCase> m_ownedPrepareAdoptionUseCase;

    // UseCase pointers used at runtime
    application::RecognizeRegionUseCase* m_ocrUseCase{nullptr};
    application::TranslatePageUseCase* m_translateUseCase{nullptr};
    application::ClearRegionUseCase* m_clearUseCase{nullptr};
    application::LayoutRegionUseCase* m_layoutUseCase{nullptr};
    application::SmartTranslatePageWorkflow* m_smartWorkflow{nullptr};
    application::PrepareProcessingAdoptionUseCase* m_prepareAdoptionUseCase{nullptr};

    QHash<WatcherKey, ActiveTask> m_activeTasks;
    QHash<domain::PageId, application::PageProcessingProposal> m_pageProposals;
    QHash<domain::PageId, application::PreparedProcessingAdoption> m_preparedAdoptions;
    QHash<domain::PageId, QString> m_pageSafeMessages;
    QHash<domain::PageId, QVector<domain::RegionId>> m_pageConflictRegions;
    QHash<domain::PageId, QVector<domain::RegionId>> m_pageFailedRegions;
};

} // namespace manga::ui::screen::editor
