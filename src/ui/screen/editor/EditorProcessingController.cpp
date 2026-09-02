#include "ui/screen/editor/EditorProcessingController.h"

#include "application/ports/ITextDetector.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/processing/ManualPaintWorkflow.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/workflow/BatchPagesProcessingWorkflow.h"
#include "application/workflow/OperationRegistry.h"
#include "application/workflow/SmartTranslatePageWorkflow.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/TextContent.h"
#include "rendering/inpaint/StrokeMaskRasterizer.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSet>

namespace manga::ui::screen::editor {

EditorProcessingController::EditorProcessingController(EditorViewModel* viewModel,
                                                       application::IOcrEngine* ocrEngine,
                                                       application::ITranslator* translator,
                                                       application::IInpainter* inpainter,
                                                       application::IPageInpainter* pageInpainter,
                                                       application::ITextLayouter* layouter,
                                                       application::IDerivedAssetStore* assetStore,
                                                       application::OperationRegistry* registry,
                                                       application::ITextDetector* textDetector,
                                                       application::BatchPagesProcessingWorkflow* batchWorkflow,
                                                       QObject* parent)
    : QObject(parent), m_viewModel(viewModel), m_registry(registry), m_assetStore(assetStore),
      m_pageInpainter(pageInpainter), m_textDetector(textDetector), m_ocrEngine(ocrEngine) {
    m_idleDetectorTimer.setSingleShot(true);
    connect(&m_idleDetectorTimer, &QTimer::timeout, this, [this]() {
        if (m_textDetector) {
            m_textDetector->release();
        }
    });

    m_idleInpaintTimer.setSingleShot(true);
    connect(&m_idleInpaintTimer, &QTimer::timeout, this, [this]() {
        if (m_pageInpainter) {
            m_pageInpainter->release();
        }
    });

    m_idleOcrTimer.setSingleShot(true);
    connect(&m_idleOcrTimer, &QTimer::timeout, this, [this]() {
        if (m_ocrEngine) {
            m_ocrEngine->release();
        }
    });

    if (m_registry != nullptr) {
        m_ownedOcrUseCase = std::make_unique<application::RecognizeRegionUseCase>(ocrEngine, m_registry);
        m_ownedTranslateUseCase = std::make_unique<application::TranslatePageUseCase>(translator, m_registry);
        m_ownedClearUseCase = std::make_unique<application::ClearRegionUseCase>(inpainter, m_registry);
        m_ownedLayoutUseCase = std::make_unique<application::LayoutRegionUseCase>(layouter, m_registry);
        m_ownedSmartWorkflow = std::make_unique<application::SmartTranslatePageWorkflow>(m_ownedOcrUseCase.get(),
                                                                                         m_ownedTranslateUseCase.get(),
                                                                                         m_ownedClearUseCase.get(),
                                                                                         m_ownedLayoutUseCase.get(),
                                                                                         m_registry,
                                                                                         this);

        m_ocrUseCase = m_ownedOcrUseCase.get();
        m_translateUseCase = m_ownedTranslateUseCase.get();
        m_clearUseCase = m_ownedClearUseCase.get();
        m_layoutUseCase = m_ownedLayoutUseCase.get();
        m_smartWorkflow = m_ownedSmartWorkflow.get();
    }

    setBatchWorkflow(batchWorkflow);

    if (m_viewModel != nullptr) {
        m_viewModel->setProcessingController(this);
    }
}

EditorProcessingController::EditorProcessingController(
    EditorViewModel* viewModel,
    application::RecognizeRegionUseCase* ocrUseCase,
    application::TranslatePageUseCase* translateUseCase,
    application::ClearRegionUseCase* clearUseCase,
    application::LayoutRegionUseCase* layoutUseCase,
    application::SmartTranslatePageWorkflow* smartWorkflow,
    application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase,
    application::OperationRegistry* registry,
    application::BatchPagesProcessingWorkflow* batchWorkflow,
    QObject* parent)
    : QObject(parent), m_viewModel(viewModel), m_registry(registry), m_ocrUseCase(ocrUseCase),
      m_translateUseCase(translateUseCase), m_clearUseCase(clearUseCase), m_layoutUseCase(layoutUseCase),
      m_smartWorkflow(smartWorkflow), m_prepareAdoptionUseCase(prepareAdoptionUseCase) {
    setBatchWorkflow(batchWorkflow);
    if (m_viewModel != nullptr) {
        m_viewModel->setProcessingController(this);
    }
}

EditorProcessingController::~EditorProcessingController() {
    releaseEngines();
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(it.value().operationId);
        }
    }
    m_activeTasks.clear();
    m_pageProposals.clear();
    m_preparedAdoptions.clear();
    m_pageConflictRegions.clear();
    m_pageSafeMessages.clear();
    m_pageFailedRegions.clear();
    if (m_viewModel && m_viewModel->processingController() == this) {
        m_viewModel->setProcessingController(nullptr);
    }
}

void EditorProcessingController::releaseEngines() {
    m_idleDetectorTimer.stop();
    m_idleInpaintTimer.stop();
    m_idleOcrTimer.stop();
    if (m_textDetector) {
        m_textDetector->release();
    }
    if (m_ocrEngine) {
        m_ocrEngine->release();
    }
    if (m_pageInpainter) {
        m_pageInpainter->release();
    }
}

void EditorProcessingController::setPageInpainter(application::IPageInpainter* pageInpainter) {
    if (m_pageInpainter == pageInpainter) {
        return;
    }
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end();) {
        if (it.key().stage == application::OperationStage::Clear && !it.key().regionId.has_value()) {
            if (m_registry != nullptr) {
                m_registry->cancelOperation(it.value().operationId);
            }
            it = m_activeTasks.erase(it);
        } else {
            ++it;
        }
    }
    m_pageInpainter = pageInpainter;
    if (auto* inpainter = dynamic_cast<application::IInpainter*>(pageInpainter)) {
        m_ownedClearUseCase = std::make_unique<application::ClearRegionUseCase>(inpainter, m_registry);
        m_clearUseCase = m_ownedClearUseCase.get();
    }
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::setTextDetector(application::ITextDetector* textDetector) {
    if (m_textDetector == textDetector) {
        return;
    }
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end();) {
        if (it.key().stage == application::OperationStage::Detect && !it.key().regionId.has_value()) {
            if (m_registry != nullptr) {
                m_registry->cancelOperation(it.value().operationId);
            }
            it = m_activeTasks.erase(it);
        } else {
            ++it;
        }
    }
    m_textDetector = textDetector;
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::setBatchWorkflow(application::BatchPagesProcessingWorkflow* batchWorkflow) {
    if (m_batchWorkflow == batchWorkflow) {
        return;
    }
    if (m_batchWorkflowConnection) {
        disconnect(m_batchWorkflowConnection);
    }
    m_batchWorkflow = batchWorkflow;
    if (m_batchWorkflow != nullptr) {
        m_batchWorkflowConnection = connect(m_batchWorkflow,
                                            &application::BatchPagesProcessingWorkflow::pageCommitted,
                                            this,
                                            [this](const domain::PageId& pageId) {
                                                if (m_viewModel != nullptr && m_viewModel->hasSession() &&
                                                    pageId == m_viewModel->state().currentPageId) {
                                                    m_viewModel->selectPage(pageId);
                                                }
                                            });
    }
}

void EditorProcessingController::cancelMatchingTask(const WatcherKey& key) {
    if (m_activeTasks.contains(key)) {
        const auto task = m_activeTasks.take(key);
        if (m_registry != nullptr) {
            m_registry->cancelOperation(task.operationId);
        }
    }
}

void EditorProcessingController::smartTranslatePage(const domain::PageId& pageId) {
    smartTranslate(pageId, {});
}

void EditorProcessingController::detectRegions(const domain::PageId& pageId, bool clearExisting) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || m_textDetector == nullptr) {
        return;
    }
    const auto& image = m_viewModel->currentPageImage();
    if (image.isNull() || !pageId.isValid()) {
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }

    WatcherKey key;
    key.projectId = project->id();
    key.pageId = pageId;
    key.stage = application::OperationStage::Detect;

    cancelMatchingTask(key);

    application::OperationDescriptor descriptor;
    descriptor.projectId = project->id();
    descriptor.pageId = pageId;
    descriptor.stage = application::OperationStage::Detect;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto operation = m_registry != nullptr
                         ? m_registry->createOperation(descriptor, sessionGen, m_viewModel->viewGeneration())
                         : application::OperationContext{application::OperationId::generate(), sessionGen};
    const auto opId = operation.operationId;

    ActiveTask task;
    task.operationId = opId;
    task.sessionGeneration = sessionGen;
    task.key = key;
    m_activeTasks[key] = task;

    application::TextDetectionRequest req;
    req.pageId = pageId;
    req.image = image;
    req.readingDirection = domain::ReadingDirection::RightToLeft;
    req.minConfidence = 0.25;

    qInfo().noquote() << "[Detect] start page=" << QString::fromStdString(pageId.toString())
                      << "opId=" << QString::fromStdString(opId.toString()) << "imgSize=" << image.size()
                      << "clearExisting=" << clearExisting;

    auto future = m_textDetector->detect(std::move(req), operation);
    auto* watcher = new QFutureWatcher<application::Result<application::TextDetectionResponse>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, key, opId, sessionGen, clearExisting]() {
        if (m_activeTasks.contains(key) && m_activeTasks[key].operationId == opId) {
            m_activeTasks.remove(key);
        }
        if (m_registry != nullptr) {
            m_registry->markFinished(opId);
        }

        if (m_viewModel != nullptr && m_viewModel->hasSession() &&
            sessionGen == m_viewModel->projectSessionGeneration()) {
            auto result = watcher->result();
            if (result.has_value()) {
                qInfo().noquote() << "[Detect] success page=" << QString::fromStdString(key.pageId.toString())
                                  << "detected=" << result->detectedRegions.size()
                                  << "panels=" << result->panels.size();
                m_viewModel->applyDetectedRegions(key.pageId, result->detectedRegions, clearExisting);
                Q_EMIT processingFinished(key.pageId, true);
            } else {
                qWarning().noquote() << "[Detect] failed page=" << QString::fromStdString(key.pageId.toString())
                                     << "code=" << result.error().code << "message=" << result.error().safeMessage;
                Q_EMIT processingFinished(key.pageId, false);
            }
        }
        watcher->deleteLater();
        updateProcessingStateForCurrentPage();
        m_idleDetectorTimer.start(60000);
    });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::smartTranslateRegions(const domain::PageId& pageId,
                                                       const QVector<domain::RegionId>& regionIds) {
    if (regionIds.isEmpty()) {
        return;
    }
    smartTranslate(pageId, regionIds);
}

void EditorProcessingController::smartTranslate(const domain::PageId& pageId,
                                                const QVector<domain::RegionId>& regionIds) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || m_smartWorkflow == nullptr ||
        m_registry == nullptr) {
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }

    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }

    if (page->regions().empty()) {
        EditorProcessingState proc;
        proc.activePageId = pageId;
        proc.status = ProcessingStatus::Idle;
        proc.isRunning = false;
        proc.hasProposal = false;
        proc.safeMessage = tr("当前页面没有文本区域，请先创建区域");
        m_pageSafeMessages[pageId] = proc.safeMessage;
        if (pageId == m_viewModel->state().currentPageId) {
            m_viewModel->updateProcessingState(proc);
            EditorNotice notice;
            notice.safeMessage = proc.safeMessage;
            notice.retryable = false;
            m_viewModel->setNotice(notice);
        }
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, std::nullopt, application::OperationStage::Translate};

    // Cancel old in-flight operation for the same target
    cancelMatchingTask(key);

    QVector<application::RegionProcessingTarget> targets;
    QHash<domain::RegionId, quint64> expectedRevisions;
    QVector<domain::RegionId> targetRegionIds;

    const QSet<domain::RegionId> requested(regionIds.cbegin(), regionIds.cend());
    for (const auto& region : page->regions()) {
        if (!requested.isEmpty() && !requested.contains(region.id())) {
            continue;
        }
        application::RegionProcessingTarget target;
        target.regionId = region.id();
        target.geometry = region.geometry();
        target.bubbleGeometry = region.bubbleGeometry();
        target.bubblePolygon = region.bubblePolygon();
        target.sourceText = QString::fromStdString(region.source().text);
        target.effectiveTranslation = QString::fromStdString(region.translation().effectiveText());
        target.contentRevision = region.contentRevision();
        target.hasManualTranslation = region.translation().hasManualText();
        targets.append(target);

        expectedRevisions.insert(region.id(), region.contentRevision());
        targetRegionIds.append(region.id());
    }

    if (targets.isEmpty()) {
        return;
    }

    const QString sourceLang = QStringLiteral("ja");
    const QString targetLang = QStringLiteral("en");

    application::OperationId opId;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    const QImage pageImage = m_viewModel->currentPageImage();
    if (pageImage.isNull()) {
        m_pageSafeMessages[pageId] = tr("页面图像尚未加载，请稍后重试");
        updateProcessingStateForCurrentPage();
        return;
    }

    auto future =
        m_smartWorkflow->execute(projectId, pageId, targets, sourceLang, targetLang, pageImage, sessionGen, opId);

    ActiveTask activeTask;
    activeTask.operationId = opId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = targetRegionIds;
    activeTask.expectedRevisions = expectedRevisions;
    m_activeTasks.insert(key, activeTask);

    auto* watcher = new QFutureWatcher<application::Result<application::PageProcessingProposal>>(this);
    connect(watcher,
            &QFutureWatcherBase::finished,
            this,
            [this, watcher, key, opId, sessionGen, targetRegionIds, expectedRevisions]() {
                handleSmartTranslateFinished(key, opId, sessionGen, watcher, targetRegionIds, expectedRevisions);
                watcher->deleteLater();
            });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::prepareAdoptionAsync(const WatcherKey& key,
                                                      const application::PrepareProcessingAdoptionRequest& prepReq,
                                                      quint64 sessionGen,
                                                      std::optional<application::OperationContext> context) {
    auto* project = m_viewModel ? m_viewModel->projectAggregate() : nullptr;
    if (project == nullptr) {
        return;
    }

    if (m_prepareAdoptionUseCase != nullptr) {
        auto prepFuture = m_prepareAdoptionUseCase->execute(prepReq);
        auto* prepWatcher = new QFutureWatcher<application::Result<application::PreparedProcessingAdoption>>(this);
        connect(prepWatcher, &QFutureWatcherBase::finished, this, [this, prepWatcher, key, sessionGen, context]() {
            handlePrepareAdoptionFinished(key, sessionGen, prepWatcher, context);
            prepWatcher->deleteLater();
        });
        prepWatcher->setFuture(prepFuture);
        return;
    }

    auto clearUseCase = std::make_shared<application::PrepareClearResultUseCase>(project, m_assetStore);
    auto prepUseCase = std::make_shared<application::PrepareProcessingAdoptionUseCase>(project, clearUseCase.get());
    auto prepFuture = prepUseCase->execute(prepReq);
    auto* prepWatcher = new QFutureWatcher<application::Result<application::PreparedProcessingAdoption>>(this);
    connect(prepWatcher,
            &QFutureWatcherBase::finished,
            this,
            [this, prepWatcher, key, sessionGen, clearUseCase, prepUseCase, context]() {
                handlePrepareAdoptionFinished(key, sessionGen, prepWatcher, context);
                prepWatcher->deleteLater();
            });
    prepWatcher->setFuture(prepFuture);
}

void EditorProcessingController::handlePrepareAdoptionFinished(
    const WatcherKey& key,
    quint64 sessionGen,
    QFutureWatcher<application::Result<application::PreparedProcessingAdoption>>* watcher,
    std::optional<application::OperationContext> context) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || watcher == nullptr) {
        return;
    }
    if (sessionGen != m_viewModel->projectSessionGeneration()) {
        return;
    }

    auto res = watcher->result();
    if (res.has_value()) {
        qInfo().noquote() << "[EditorProcessingController] Prepared adoption success for page:"
                          << QString::fromStdString(key.pageId.toString());
        m_preparedAdoptions[key.pageId] = res.value();
        if (key.pageId == m_viewModel->state().currentPageId) {
            updateProcessingStateForCurrentPage();
        }
        if (context.has_value() && context->progressReporter) {
            context->progressReporter->markFinished();
        }
        Q_EMIT proposalReady(key.pageId);

        adoptProposal(key.pageId);

        Q_EMIT processingFinished(key.pageId, true);
    } else {
        const auto& err = res.error();
        qWarning().noquote() << "[EditorProcessingController] Prepared adoption failed:" << err.safeMessage;
        if (context.has_value() && context->progressReporter) {
            context->progressReporter->markFinished(err);
        }
        if (err.category != application::ErrorCategory::Cancelled) {
            m_pageFailedRegions[key.pageId] = {key.regionId.value_or(domain::RegionId{})};
            m_pageSafeMessages[key.pageId] = err.safeMessage;
            if (key.pageId == m_viewModel->state().currentPageId) {
                updateProcessingStateForCurrentPage();
            }
            Q_EMIT processingFinished(key.pageId, false);
        }
    }
}

void EditorProcessingController::handleSmartTranslateFinished(
    const WatcherKey& key,
    application::OperationId operationId,
    quint64 sessionGen,
    QFutureWatcher<application::Result<application::PageProcessingProposal>>* watcher,
    QVector<domain::RegionId> targetRegions,
    QHash<domain::RegionId, quint64> expectedRevisions) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || watcher == nullptr) {
        return;
    }
    if (sessionGen != m_viewModel->projectSessionGeneration()) {
        return; // Stale session
    }
    if (!m_activeTasks.contains(key) || m_activeTasks.value(key).operationId != operationId) {
        return; // Stale/superseded task
    }

    m_activeTasks.remove(key);

    auto result = watcher->result();
    if (!result.has_value()) {
        const auto& err = result.error();
        if (err.category != application::ErrorCategory::Cancelled) {
            m_pageFailedRegions[key.pageId] = targetRegions;
            m_pageSafeMessages[key.pageId] = err.safeMessage;
            if (key.pageId == m_viewModel->state().currentPageId) {
                updateProcessingStateForCurrentPage();
            }
            Q_EMIT processingFinished(key.pageId, false);
        }
        return;
    }

    auto proposal = result.value();
    m_pageProposals[key.pageId] = proposal;

    QVector<domain::RegionId> conflictRegions;
    QString safeMsg;

    for (const auto& issue : proposal.issues) {
        if (issue.regionId.has_value()) {
            conflictRegions.append(*issue.regionId);
        }
        if (safeMsg.isEmpty() && !issue.error.safeMessage.isEmpty()) {
            safeMsg = issue.error.safeMessage;
        }
    }

    auto* project = m_viewModel->projectAggregate();
    const auto* page = project ? project->findPage(key.pageId) : nullptr;
    if (page != nullptr) {
        for (const auto& rId : targetRegions) {
            const auto* r = page->findRegion(rId);
            if (r != nullptr && r->translation().hasManualText()) {
                if (!conflictRegions.contains(rId)) {
                    conflictRegions.append(rId);
                }
            }
        }
    }

    if (!conflictRegions.isEmpty()) {
        safeMsg = tr("有新的 AI 译文可比较");
    }

    m_pageConflictRegions[key.pageId] = conflictRegions;
    m_pageSafeMessages[key.pageId] = safeMsg;

    application::PrepareProcessingAdoptionRequest prepReq;
    prepReq.projectId = key.projectId;
    prepReq.pageId = key.pageId;
    prepReq.stage = application::OperationStage::Translate;
    prepReq.sessionGeneration = sessionGen;
    prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
    prepReq.ocr = proposal.ocr;
    prepReq.translation = proposal.translation;
    prepReq.clear = proposal.clear;
    prepReq.layout = proposal.layout;
    prepReq.expectedRevisions = expectedRevisions;
    prepReq.projectRoot = m_viewModel->projectRootPath();
    if (page != nullptr) {
        prepReq.pageSize = QSize(page->imageSize().width, page->imageSize().height);
    }
    prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

    prepareAdoptionAsync(key, prepReq, sessionGen);
}

void EditorProcessingController::recognizeRegion(const domain::PageId& pageId, const domain::RegionId& regionId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || !regionId.isValid() ||
        m_ocrUseCase == nullptr || m_registry == nullptr) {
        qWarning() << "[OCR] rejected invalid controller/session/request state";
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }
    const auto* region = page->findRegion(regionId);
    if (region == nullptr) {
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, regionId, application::OperationStage::Ocr};
    cancelMatchingTask(key);

    application::OcrRequest request;
    request.pageId = pageId;
    request.regionId = regionId;
    request.rect = region->geometry();
    request.image = m_viewModel->currentPageImage();
    if (project) {
        request.language = project->sourceLanguage();
    }
    if (request.image.isNull()) {
        qWarning().noquote() << "[OCR] rejected: current page image unavailable page="
                             << QString::fromStdString(pageId.toString());
        return;
    }

    qInfo().noquote() << "[OCR] start page=" << QString::fromStdString(pageId.toString())
                      << "region=" << QString::fromStdString(regionId.toString()) << "image=" << request.image.size()
                      << "rect=" << request.rect.x << request.rect.y << request.rect.width << request.rect.height;

    application::OperationDescriptor desc;
    desc.projectId = projectId;
    desc.pageId = pageId;
    desc.regionId = regionId;
    desc.stage = application::OperationStage::Ocr;

    application::OperationId opId;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto future = m_ocrUseCase->execute(request, desc, sessionGen, opId);

    ActiveTask activeTask;
    activeTask.operationId = opId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = {regionId};
    activeTask.expectedRevisions.insert(regionId, region->contentRevision());
    m_activeTasks.insert(key, activeTask);

    auto* watcher = new QFutureWatcher<application::Result<application::OcrResponse>>(this);
    connect(watcher,
            &QFutureWatcherBase::finished,
            this,
            [this, watcher, key, opId, sessionGen, regionId, rev = region->contentRevision()]() {
                qInfo().noquote() << "[OCR] watcher finished page=" << QString::fromStdString(key.pageId.toString())
                                  << "region=" << QString::fromStdString(regionId.toString())
                                  << "operation=" << QString::fromStdString(opId.toString());
                if (m_viewModel != nullptr && m_viewModel->hasSession() &&
                    sessionGen == m_viewModel->projectSessionGeneration() && m_activeTasks.contains(key) &&
                    m_activeTasks.value(key).operationId == opId) {
                    m_activeTasks.remove(key);
                    auto res = watcher->result();
                    if (res.has_value()) {
                        qInfo().noquote() << "[OCR] success page=" << QString::fromStdString(key.pageId.toString())
                                          << "region=" << QString::fromStdString(regionId.toString())
                                          << "textLength=" << res.value().text.size();
                        auto ocrResp = res.value();
                        application::PageProcessingProposal prop;
                        prop.projectId = key.projectId;
                        prop.pageId = key.pageId;
                        prop.rootOperationId = opId;
                        prop.ocr.append(ocrResp);
                        m_pageProposals[key.pageId] = prop;

                        application::PrepareProcessingAdoptionRequest prepReq;
                        prepReq.projectId = key.projectId;
                        prepReq.pageId = key.pageId;
                        prepReq.stage = application::OperationStage::Ocr;
                        prepReq.sessionGeneration = sessionGen;
                        prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                        prepReq.ocr.append(ocrResp);
                        prepReq.expectedRevisions.insert(regionId, rev);
                        prepReq.projectRoot = m_viewModel->projectRootPath();
                        prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

                        prepareAdoptionAsync(key, prepReq, sessionGen);
                        m_registry->markFinished(opId);
                    } else {
                        const auto& err = res.error();
                        m_registry->markFinished(opId, err);
                        qWarning().noquote() << "[OCR] failed page=" << QString::fromStdString(key.pageId.toString())
                                             << "region=" << QString::fromStdString(regionId.toString())
                                             << "category=" << static_cast<int>(err.category) << "code=" << err.code
                                             << "message=" << err.safeMessage;
                        if (err.category == application::ErrorCategory::Model ||
                            err.code.startsWith(QLatin1String("model."))) {
                            m_viewModel->setOcrAvailability(false, err.safeMessage);
                        }
                        if (err.category != application::ErrorCategory::Cancelled) {
                            m_pageFailedRegions[key.pageId] = {regionId};
                            m_pageSafeMessages[key.pageId] = err.safeMessage;
                            if (key.pageId == m_viewModel->state().currentPageId) {
                                updateProcessingStateForCurrentPage();
                            }
                            Q_EMIT processingFinished(key.pageId, false);
                        }
                    }
                }
                m_idleOcrTimer.start(60000);
                watcher->deleteLater();
            });
    watcher->setFuture(future);
    qInfo().noquote() << "[OCR] watcher attached page=" << QString::fromStdString(pageId.toString())
                      << "region=" << QString::fromStdString(regionId.toString())
                      << "operation=" << QString::fromStdString(opId.toString())
                      << "futureFinished=" << future.isFinished();

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::recognizePage(const domain::PageId& pageId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid()) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }
    QVector<domain::RegionId> targetIds;
    for (const auto& r : page->regions()) {
        if (r.isEnabled()) {
            targetIds.append(r.id());
        }
    }
    recognizeRegions(pageId, targetIds);
}

void EditorProcessingController::recognizeSelectedRegions() {
    if (m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    const auto selected = m_viewModel->state().selectedRegionIds;
    recognizeRegions(pageId, selected);
}

void EditorProcessingController::recognizeRegions(const domain::PageId& pageId,
                                                  const QVector<domain::RegionId>& regionIds) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || regionIds.isEmpty() ||
        m_ocrUseCase == nullptr || m_registry == nullptr) {
        return;
    }

    if (regionIds.size() == 1) {
        recognizeRegion(pageId, regionIds[0]);
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }

    QVector<const domain::TextRegion*> validRegions;
    for (const auto& rId : regionIds) {
        const auto* r = page->findRegion(rId);
        if (r != nullptr && r->isEnabled()) {
            validRegions.append(r);
        }
    }
    if (validRegions.isEmpty()) {
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, std::nullopt, application::OperationStage::Ocr};
    cancelMatchingTask(key);

    const QImage pageImage = m_viewModel->currentPageImage();
    if (pageImage.isNull()) {
        qWarning().noquote() << "[OCR] rejected: current page image unavailable page="
                             << QString::fromStdString(pageId.toString());
        return;
    }

    application::OperationDescriptor rootDesc{
        .stage = application::OperationStage::Ocr,
        .projectId = projectId,
        .pageId = pageId,
        .regionId = std::nullopt,
        .displayTarget = tr("批量文字识别"),
        .cancellable = true,
    };

    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto rootContext = m_registry->createOperation(std::move(rootDesc), sessionGen, m_viewModel->viewGeneration());
    const auto rootOpId = rootContext.operationId;

    if (rootContext.progressReporter) {
        rootContext.progressReporter->setRange(validRegions.size());
        rootContext.progressReporter->setValue(0);
        rootContext.progressReporter->setStatusText(tr("正在识别 (0/%1)").arg(validRegions.size()));
        rootContext.progressReporter->markRunning();
    }

    QVector<domain::RegionId> targetRegionIds;
    QHash<domain::RegionId, quint64> expectedRevisions;
    for (const auto* r : validRegions) {
        targetRegionIds.append(r->id());
        expectedRevisions.insert(r->id(), r->contentRevision());
    }

    ActiveTask activeTask;
    activeTask.operationId = rootOpId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = targetRegionIds;
    activeTask.expectedRevisions = expectedRevisions;
    m_activeTasks.insert(key, activeTask);

    struct BatchState {
        int total{0};
        int completed{0};
        QVector<application::OcrResponse> responses;
        QVector<domain::RegionId> failedRegions;
        application::ApplicationError lastError;
    };
    auto batchState = std::make_shared<BatchState>();
    batchState->total = validRegions.size();

    for (const auto* region : validRegions) {
        const auto rId = region->id();
        application::OcrRequest request;
        request.pageId = pageId;
        request.regionId = rId;
        request.rect = region->geometry();
        request.image = pageImage;
        if (project) {
            request.language = project->sourceLanguage();
        }

        auto childContext = rootContext;
        // Batch progress belongs to the controller. Do not let a child decoder replace the
        // root range with its local 0..100 range or settle the shared operation.
        childContext.progressReporter.reset();
        auto future = m_ocrUseCase->execute(request, childContext);
        auto* watcher = new QFutureWatcher<application::Result<application::OcrResponse>>(this);
        connect(watcher,
                &QFutureWatcherBase::finished,
                this,
                [this, watcher, key, rootOpId, rootContext, sessionGen, rId, batchState, expectedRevisions]() mutable {
                    qInfo().noquote() << "[EditorProcessingController] batch OCR watcher finished page="
                                      << QString::fromStdString(key.pageId.toString())
                                      << "region=" << QString::fromStdString(rId.toString())
                                      << "rootOperation=" << QString::fromStdString(rootOpId.toString());
                    auto res = watcher->result();
                    batchState->completed++;
                    qInfo().noquote() << "[EditorProcessingController] OCR completed for region:"
                                      << QString::fromStdString(rId.toString()) << "progress:" << batchState->completed
                                      << "/" << batchState->total;

                    if (rootContext.progressReporter) {
                        rootContext.progressReporter->setValue(batchState->completed);
                        rootContext.progressReporter->setStatusText(
                            tr("正在识别 (%1/%2)").arg(batchState->completed).arg(batchState->total));
                    }

                    if (res.has_value()) {
                        qInfo().noquote()
                            << "[EditorProcessingController] OCR success. Text:" << res.value().text.left(30);
                        batchState->responses.append(res.value());
                    } else {
                        qWarning().noquote() << "[EditorProcessingController] OCR failed:" << res.error().safeMessage;
                        batchState->failedRegions.append(rId);
                        batchState->lastError = res.error();
                    }

                    if (batchState->completed >= batchState->total) {
                        qInfo().noquote()
                            << "[EditorProcessingController] Batch complete. Validating state for adoption...";
                        bool hasSession = m_viewModel != nullptr && m_viewModel->hasSession();
                        bool genMatch = sessionGen == (m_viewModel ? m_viewModel->projectSessionGeneration() : 0);
                        bool hasTask = m_activeTasks.contains(key);
                        bool taskMatch = hasTask && m_activeTasks.value(key).operationId == rootOpId;

                        qInfo().noquote()
                            << "[EditorProcessingController] State validation - hasSession:" << hasSession
                            << "genMatch:" << genMatch << "hasTask:" << hasTask << "taskMatch:" << taskMatch;

                        if (hasSession && genMatch && taskMatch) {
                            qInfo().noquote() << "[EditorProcessingController] Proceeding with adoption.";
                            m_activeTasks.remove(key);

                            if (!batchState->responses.isEmpty()) {
                                if (rootContext.progressReporter) {
                                    rootContext.progressReporter->setValue(batchState->total);
                                    rootContext.progressReporter->setStatusText(tr("正在应用识别结果..."));
                                }
                                application::PageProcessingProposal prop;
                                prop.projectId = key.projectId;
                                prop.pageId = key.pageId;
                                prop.rootOperationId = rootOpId;
                                prop.ocr = batchState->responses;
                                m_pageProposals[key.pageId] = prop;

                                application::PrepareProcessingAdoptionRequest prepReq;
                                prepReq.projectId = key.projectId;
                                prepReq.pageId = key.pageId;
                                prepReq.stage = application::OperationStage::Ocr;
                                prepReq.sessionGeneration = sessionGen;
                                prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                                prepReq.ocr = batchState->responses;
                                prepReq.expectedRevisions = expectedRevisions;
                                prepReq.projectRoot = m_viewModel->projectRootPath();
                                prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

                                prepareAdoptionAsync(key, prepReq, sessionGen, rootContext);
                            } else if (!batchState->failedRegions.isEmpty()) {
                                if (rootContext.progressReporter) {
                                    rootContext.progressReporter->markFinished(batchState->lastError);
                                }
                                if (batchState->lastError.category != application::ErrorCategory::Cancelled) {
                                    m_pageFailedRegions[key.pageId] = batchState->failedRegions;
                                    m_pageSafeMessages[key.pageId] = batchState->lastError.safeMessage;
                                    if (key.pageId == m_viewModel->state().currentPageId) {
                                        updateProcessingStateForCurrentPage();
                                    }
                                    Q_EMIT processingFinished(key.pageId, false);
                                }
                            }
                        }
                    }
                    m_idleOcrTimer.start(60000);
                    watcher->deleteLater();
                });
        watcher->setFuture(future);
        qInfo().noquote() << "[EditorProcessingController] batch OCR watcher attached page="
                          << QString::fromStdString(pageId.toString())
                          << "region=" << QString::fromStdString(rId.toString())
                          << "rootOperation=" << QString::fromStdString(rootOpId.toString())
                          << "futureFinished=" << future.isFinished();
    }

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, rootOpId);
}

void EditorProcessingController::translatePage(const domain::PageId& pageId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid()) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }
    QVector<domain::RegionId> regionIds;
    for (const auto& r : page->regions()) {
        if (r.isEnabled()) {
            regionIds.append(r.id());
        }
    }
    translateRegions(pageId, regionIds);
}

void EditorProcessingController::translateRegions(const domain::PageId& pageId,
                                                  const QVector<domain::RegionId>& regionIds) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || regionIds.isEmpty() ||
        m_translateUseCase == nullptr || m_registry == nullptr) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }

    application::TranslationRequest req;
    req.sourceLanguage =
        project->sourceLanguage().empty() ? domain::LanguageCode::createUnchecked("ja") : project->sourceLanguage();
    req.targetLanguage =
        project->targetLanguage().empty() ? domain::LanguageCode::createUnchecked("zh-CN") : project->targetLanguage();
    req.readingDirection = project->readingDirection();

    QVector<domain::RegionId> validRegions;
    QHash<domain::RegionId, quint64> expectedRevisions;

    for (const auto& rId : regionIds) {
        const auto* r = page->findRegion(rId);
        if (r != nullptr && r->isEnabled()) {
            const QString sourceStr = QString::fromStdString(r->source().text).trimmed();
            const QString manualStr = r->translation().hasManualText()
                                          ? QString::fromStdString(r->translation().manualText().value()).trimmed()
                                          : QString();
            const QString text = !sourceStr.isEmpty() ? sourceStr : manualStr;
            if (!text.isEmpty()) {
                req.regions.append({rId, text});
                validRegions.append(rId);
                expectedRevisions[rId] = r->contentRevision();
            }
        }
    }

    if (validRegions.isEmpty()) {
        qInfo() << "[Translate] no non-empty text regions to translate on page"
                << QString::fromStdString(pageId.toString());
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, std::nullopt, application::OperationStage::Translate};
    cancelMatchingTask(key);

    application::OperationDescriptor descriptor{
        application::OperationStage::Translate, projectId, pageId, std::nullopt, QStringLiteral("Translate"), true};
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    application::OperationId opId;
    auto future = m_translateUseCase->execute(req, descriptor, sessionGen, opId);

    ActiveTask activeTask;
    activeTask.operationId = opId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = validRegions;
    m_activeTasks.insert(key, activeTask);

    auto* watcher = new QFutureWatcher<application::Result<application::TranslationResponse>>(this);
    connect(watcher,
            &QFutureWatcherBase::finished,
            this,
            [this, watcher, key, sessionGen, validRegions, expectedRevisions, opId]() {
                if (m_activeTasks.contains(key) && m_activeTasks[key].operationId == opId) {
                    m_activeTasks.remove(key);
                }
                if (m_registry != nullptr) {
                    m_registry->markFinished(opId);
                }

                if (m_viewModel == nullptr || !m_viewModel->hasSession() ||
                    sessionGen != m_viewModel->projectSessionGeneration()) {
                    watcher->deleteLater();
                    return;
                }

                auto res = watcher->result();
                if (res.has_value()) {
                    application::PageProcessingProposal proposal;
                    proposal.projectId = key.projectId;
                    proposal.pageId = key.pageId;
                    proposal.rootOperationId = opId;
                    proposal.translation = res.value();

                    m_pageProposals[key.pageId] = proposal;

                    auto* curProject = m_viewModel->projectAggregate();
                    const auto* curPage = curProject ? curProject->findPage(key.pageId) : nullptr;

                    application::PrepareProcessingAdoptionRequest prepReq;
                    prepReq.projectId = key.projectId;
                    prepReq.pageId = key.pageId;
                    prepReq.stage = application::OperationStage::Translate;
                    prepReq.sessionGeneration = sessionGen;
                    prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                    prepReq.translation = res.value();
                    prepReq.expectedRevisions = expectedRevisions;
                    prepReq.projectRoot = m_viewModel->projectRootPath();
                    if (curPage != nullptr) {
                        prepReq.pageSize = QSize(curPage->imageSize().width, curPage->imageSize().height);
                    }
                    prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

                    prepareAdoptionAsync(key, prepReq, sessionGen);
                } else {
                    const auto& err = res.error();
                    if (err.category != application::ErrorCategory::Cancelled) {
                        m_pageFailedRegions[key.pageId] = validRegions;
                        m_pageSafeMessages[key.pageId] = err.safeMessage;
                        if (key.pageId == m_viewModel->state().currentPageId) {
                            updateProcessingStateForCurrentPage();
                        }
                        Q_EMIT processingFinished(key.pageId, false);
                    }
                }
                watcher->deleteLater();
            });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::clearPage(const domain::PageId& pageId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid()) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }
    QVector<domain::RegionId> targetIds;
    for (const auto& r : page->regions()) {
        if (r.isEnabled()) {
            targetIds.append(r.id());
        }
    }
    clearRegions(pageId, targetIds);
}

void EditorProcessingController::clearSelectedRegions() {
    if (m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    const auto selected = m_viewModel->state().selectedRegionIds;
    clearRegions(pageId, selected);
}

void EditorProcessingController::inpaintStroke(const domain::PageId& pageId,
                                               const QVector<QPointF>& points,
                                               double diameter) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || m_registry == nullptr ||
        m_assetStore == nullptr || points.isEmpty())
        return;
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr || pageId != m_viewModel->state().currentPageId)
        return;
    const auto* page = project->findPage(pageId);
    const QImage source = m_viewModel->currentPageImage();
    if (page == nullptr || source.isNull())
        return;
    const auto raster = rendering::StrokeMaskRasterizer::rasterize(source.size(), points, diameter);
    if (raster.mask.isNull() || !raster.dirtyBounds.isValid())
        return;

    const WatcherKey key{project->id(), pageId, std::nullopt, application::OperationStage::Clear};
    // A page may have only one in-flight inpaint stroke. Unlike region clear, a new
    // stroke must not cancel or replace the existing job.
    if (m_activeTasks.contains(key))
        return;
    application::OperationDescriptor descriptor;
    descriptor.projectId = project->id();
    descriptor.pageId = pageId;
    descriptor.stage = application::OperationStage::Clear;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto operation = m_registry->createOperation(descriptor, sessionGen, m_viewModel->viewGeneration());
    auto workflow = std::make_shared<application::ManualInpaintWorkflow>(m_assetStore, m_pageInpainter);
    application::ManualPageInpaintRequest request;
    request.projectId = project->id();
    request.pageId = pageId;
    request.pageImage = source;
    request.mask = raster.mask;
    request.dirtyBounds = raster.dirtyBounds;
    request.projectRoot = m_viewModel->projectRootPath();
    request.expectedSessionGeneration = sessionGen;
    request.currentSessionGeneration = sessionGen;
    request.expectedPageSize = source.size();
    request.expectedClearLayerRef =
        page->clearLayerAsset().has_value() ? QString::fromStdString(*page->clearLayerAsset()) : QString();
    request.context = operation;
    qInfo().noquote() << "[Inpaint] start stroke page=" << QString::fromStdString(pageId.toString())
                      << "points=" << points.size() << "diameter=" << diameter << "dirtyBounds=" << raster.dirtyBounds;

    const auto opId = operation.operationId;
    auto future = workflow->execute(std::move(request));
    ActiveTask active{opId, sessionGen, key, {}, {}};
    m_activeTasks.insert(key, active);
    auto* watcher = new QFutureWatcher<application::Result<application::PreparedPageInpaintChange>>(this);
    QPointer<EditorProcessingController> self(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [self, watcher, key, opId, sessionGen]() {
        if (self == nullptr)
            return;
        auto* vm = self->m_viewModel;
        if (vm != nullptr && vm->hasSession() && sessionGen == vm->projectSessionGeneration() &&
            self->m_activeTasks.contains(key) && self->m_activeTasks.value(key).operationId == opId) {
            self->m_activeTasks.remove(key);
            const auto result = watcher->result();
            bool snapshotStillCurrent = false;
            if (result.has_value()) {
                const auto* currentProject = vm->projectAggregate();
                const auto* currentPage = currentProject != nullptr ? currentProject->findPage(key.pageId) : nullptr;
                const QSize currentPageSize =
                    currentPage != nullptr ? QSize(currentPage->imageSize().width, currentPage->imageSize().height)
                                           : QSize();
                const QString currentClearLayerRef =
                    currentPage != nullptr && currentPage->clearLayerAsset().has_value()
                        ? QString::fromStdString(*currentPage->clearLayerAsset())
                        : QString();
                snapshotStillCurrent =
                    currentProject != nullptr && currentProject->id() == key.projectId && currentPage != nullptr &&
                    sessionGen == vm->projectSessionGeneration() && key.pageId == vm->state().currentPageId &&
                    currentPageSize == result->expectedPageSize && currentClearLayerRef == result->beforeRef;
            }
            if (result.has_value() && snapshotStillCurrent && vm->applyPageInpaintChange(result.value())) {
                qInfo().noquote() << "[Inpaint] success page=" << QString::fromStdString(key.pageId.toString())
                                  << "layerRef="
                                  << (result->afterRef.isEmpty() ? QStringLiteral("<empty>") : result->afterRef);
                self->m_registry->markFinished(opId);
                self->updateProcessingStateForCurrentPage();
                Q_EMIT self->processingFinished(key.pageId, true);
            } else if (!result.has_value() && !result.error().isCancelled()) {
                qWarning().noquote() << "[Inpaint] failed page=" << QString::fromStdString(key.pageId.toString())
                                     << "code=" << result.error().code << "message=" << result.error().safeMessage;
                self->m_registry->markFinished(opId, result.error());
                vm->setNotice(EditorNotice{result.error().safeMessage,
                                           QString(),
                                           application::RecoveryAction::None,
                                           result.error().retryable});
                self->updateProcessingStateForCurrentPage();
                Q_EMIT self->processingFinished(key.pageId, false);
            } else if (result.has_value() && !snapshotStillCurrent) {
                self->m_registry->markFinished(
                    opId,
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.stale_clear_layer"),
                                                      QStringLiteral("Clear layer changed before commit")));
            }
        }
        self->m_idleInpaintTimer.start(60000);
        watcher->deleteLater();
    });
    watcher->setFuture(future);
    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::paintStroke(const domain::PageId& pageId,
                                             const QVector<QPointF>& points,
                                             application::ManualStrokeMode mode,
                                             const QColor& color,
                                             double diameter) {
    if (m_viewModel == nullptr || m_assetStore == nullptr || m_registry == nullptr || points.isEmpty() ||
        diameter < 1.0) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr || pageId != m_viewModel->state().currentPageId) {
        return;
    }
    const auto* page = project->findPage(pageId);
    const QImage source = m_viewModel->currentPageImage();
    if (page == nullptr || source.isNull()) {
        return;
    }

    const WatcherKey key{project->id(), pageId, std::nullopt, application::OperationStage::Clear};
    if (m_activeTasks.contains(key)) {
        return;
    }
    application::OperationDescriptor descriptor;
    descriptor.projectId = project->id();
    descriptor.pageId = pageId;
    descriptor.stage = application::OperationStage::Clear;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto operation = m_registry->createOperation(descriptor, sessionGen, m_viewModel->viewGeneration());
    auto workflow = std::make_shared<application::ManualPaintWorkflow>(m_assetStore);
    application::ManualPagePaintRequest request;
    request.mode = mode;
    request.projectId = project->id();
    request.pageId = pageId;
    request.points = points;
    request.color = color;
    request.diameter = diameter;
    request.projectRoot = m_viewModel->projectRootPath();
    request.expectedSessionGeneration = sessionGen;
    request.currentSessionGeneration = sessionGen;
    request.expectedPageSize = source.size();
    request.expectedManualLayerRef = page->manualOverrideLayerAsset().has_value()
                                         ? QString::fromStdString(*page->manualOverrideLayerAsset())
                                         : QString();
    request.context = operation;
    qInfo().noquote() << "[ManualPaint] start stroke page=" << QString::fromStdString(pageId.toString())
                      << "mode=" << (request.mode == application::ManualStrokeMode::Erase ? "Erase" : "Paint")
                      << "points=" << points.size() << "diameter=" << diameter << "color=" << color.name();

    const auto opId = operation.operationId;
    auto future = workflow->execute(std::move(request));
    ActiveTask active{opId, sessionGen, key, {}, {}};
    m_activeTasks.insert(key, active);
    auto* watcher = new QFutureWatcher<application::Result<application::PreparedPageManualOverrideChange>>(this);
    QPointer<EditorProcessingController> self(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [self, watcher, key, opId, sessionGen]() {
        if (self == nullptr) {
            return;
        }
        auto* vm = self->m_viewModel;
        if (vm != nullptr && vm->hasSession() && sessionGen == vm->projectSessionGeneration() &&
            self->m_activeTasks.contains(key) && self->m_activeTasks.value(key).operationId == opId) {
            self->m_activeTasks.remove(key);
            const auto result = watcher->result();
            bool snapshotStillCurrent = false;
            if (result.has_value()) {
                const auto* currentProject = vm->projectAggregate();
                const auto* currentPage = currentProject != nullptr ? currentProject->findPage(key.pageId) : nullptr;
                const QSize currentPageSize =
                    currentPage != nullptr ? QSize(currentPage->imageSize().width, currentPage->imageSize().height)
                                           : QSize();
                const QString currentRef = currentPage != nullptr && currentPage->manualOverrideLayerAsset().has_value()
                                               ? QString::fromStdString(*currentPage->manualOverrideLayerAsset())
                                               : QString();
                snapshotStillCurrent = currentProject != nullptr && currentProject->id() == key.projectId &&
                                       currentPage != nullptr && sessionGen == vm->projectSessionGeneration() &&
                                       key.pageId == vm->state().currentPageId &&
                                       currentPageSize == result->expectedPageSize && currentRef == result->beforeRef;
            }
            if (result.has_value() && snapshotStillCurrent && vm->applyPageManualOverrideChange(result.value())) {
                qInfo().noquote() << "[ManualPaint] success page=" << QString::fromStdString(key.pageId.toString())
                                  << "layerRef="
                                  << (result->afterRef.isEmpty() ? QStringLiteral("<empty>") : result->afterRef);
                self->m_registry->markFinished(opId);
                self->updateProcessingStateForCurrentPage();
                Q_EMIT self->processingFinished(key.pageId, true);
            } else if (!result.has_value() && !result.error().isCancelled()) {
                qWarning().noquote() << "[ManualPaint] failed page=" << QString::fromStdString(key.pageId.toString())
                                     << "code=" << result.error().code << "message=" << result.error().safeMessage;
                self->m_registry->markFinished(opId, result.error());
                vm->setNotice(EditorNotice{result.error().safeMessage,
                                           QString(),
                                           application::RecoveryAction::None,
                                           result.error().retryable});
                self->updateProcessingStateForCurrentPage();
                Q_EMIT self->processingFinished(key.pageId, false);
            } else if (result.has_value() && !snapshotStillCurrent) {
                self->m_registry->markFinished(
                    opId,
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.stale_manual_override_layer"),
                                                      QStringLiteral("Manual override layer changed before commit")));
            }
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::clearRegions(const domain::PageId& pageId,
                                              const QVector<domain::RegionId>& regionIds) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || regionIds.isEmpty() ||
        m_clearUseCase == nullptr || m_registry == nullptr) {
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }

    const auto projectId = project->id();
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    const WatcherKey key{projectId, pageId, std::nullopt, application::OperationStage::Clear};
    cancelMatchingTask(key);

    QImage currentImg = m_viewModel->currentPageImage();
    if (currentImg.isNull() || pageId != m_viewModel->state().currentPageId) {
        const int pw = page->imageSize().width > 0 ? page->imageSize().width : 800;
        const int ph = page->imageSize().height > 0 ? page->imageSize().height : 1200;
        currentImg = QImage(pw, ph, QImage::Format_RGB32);
        currentImg.fill(Qt::white);
    }

    QVector<domain::RegionId> validRegionIds;
    QHash<domain::RegionId, quint64> expectedRevisions;
    for (const auto& rId : regionIds) {
        const auto* region = page->findRegion(rId);
        if (region != nullptr && !region->isLocked()) {
            validRegionIds.append(rId);
            expectedRevisions.insert(rId, region->contentRevision());
        }
    }

    if (validRegionIds.isEmpty()) {
        return;
    }

    struct BatchState {
        int total{0};
        int completed{0};
        QVector<application::ClearResponse> responses;
        QVector<domain::RegionId> failedRegions;
        application::ApplicationError lastError;
    };
    auto batchState = std::make_shared<BatchState>();
    batchState->total = validRegionIds.size();

    application::OperationDescriptor rootDesc;
    rootDesc.stage = application::OperationStage::Clear;
    rootDesc.projectId = projectId;
    rootDesc.pageId = pageId;
    rootDesc.regionId = (validRegionIds.size() == 1 ? std::make_optional(validRegionIds.front()) : std::nullopt);
    rootDesc.displayTarget = (validRegionIds.size() == 1 ? tr("当前区域") : tr("全页背景清除"));
    rootDesc.cancellable = true;

    auto rootContext = m_registry ? m_registry->createOperation(std::move(rootDesc),
                                                                sessionGen,
                                                                m_viewModel ? m_viewModel->viewGeneration() : 0)
                                  : application::OperationContext{};
    const application::OperationId rootOpId = rootContext.operationId;
    if (rootContext.progressReporter) {
        rootContext.progressReporter->setRange(batchState->total);
        rootContext.progressReporter->setValue(0);
        rootContext.progressReporter->setStatusText(tr("正在清除 (0/%1)").arg(batchState->total));
        rootContext.progressReporter->markRunning();
    }

    ActiveTask activeTask;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.operationId = rootOpId;
    activeTask.targetRegionIds = validRegionIds;
    activeTask.expectedRevisions = expectedRevisions;
    m_activeTasks.insert(key, activeTask);

    for (const auto& rId : validRegionIds) {
        const auto* region = page->findRegion(rId);
        if (region == nullptr) {
            batchState->completed++;
            continue;
        }

        application::ClearRequest request;
        request.projectId = projectId;
        request.pageId = pageId;
        request.regionId = rId;
        request.region = region->geometry();
        request.polygon = region->bubblePolygon();
        request.settings = region->clear();
        if (!request.settings.enabled) {
            request.settings.enabled = true;
        }
        request.pageImage = currentImg;
        request.contentRevision = region->contentRevision();
        auto childContext = rootContext;
        childContext.progressReporter.reset();
        auto future = m_clearUseCase ? m_clearUseCase->execute(request, childContext)
                                     : application::makeFailedFuture<application::ClearResponse>(
                                           application::makeApplicationError(application::ErrorCategory::Internal,
                                                                             QStringLiteral("clear.unavailable"),
                                                                             QStringLiteral("Inpainter unavailable")));

        auto* watcher = new QFutureWatcher<application::Result<application::ClearResponse>>(this);
        connect(watcher,
                &QFutureWatcherBase::finished,
                this,
                [this, watcher, key, rootOpId, rootContext, sessionGen, rId, batchState, expectedRevisions]() mutable {
                    auto res = watcher->result();
                    batchState->completed++;
                    if (rootContext.progressReporter) {
                        rootContext.progressReporter->setValue(batchState->completed);
                        rootContext.progressReporter->setStatusText(
                            tr("正在清除 (%1/%2)").arg(batchState->completed).arg(batchState->total));
                    }

                    if (res.has_value()) {
                        batchState->responses.append(res.value());
                    } else {
                        batchState->failedRegions.append(rId);
                        batchState->lastError = res.error();
                    }

                    if (batchState->completed >= batchState->total) {
                        if (m_viewModel != nullptr && m_viewModel->hasSession() &&
                            sessionGen == m_viewModel->projectSessionGeneration() && m_activeTasks.contains(key) &&
                            m_activeTasks.value(key).operationId == rootOpId) {
                            m_activeTasks.remove(key);

                            if (!batchState->responses.isEmpty()) {
                                if (rootContext.progressReporter) {
                                    rootContext.progressReporter->setValue(batchState->total);
                                    rootContext.progressReporter->setStatusText(tr("正在应用并保存图层..."));
                                }
                                application::PageProcessingProposal prop;
                                prop.projectId = key.projectId;
                                prop.pageId = key.pageId;
                                prop.rootOperationId = rootOpId;
                                prop.clear = batchState->responses;
                                m_pageProposals[key.pageId] = prop;

                                application::PrepareProcessingAdoptionRequest prepReq;
                                prepReq.projectId = key.projectId;
                                prepReq.pageId = key.pageId;
                                prepReq.stage = application::OperationStage::Clear;
                                prepReq.sessionGeneration = sessionGen;
                                prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                                prepReq.clear = batchState->responses;
                                prepReq.expectedRevisions = expectedRevisions;
                                prepReq.projectRoot = m_viewModel->projectRootPath();
                                auto* proj = m_viewModel->projectAggregate();
                                const auto* pg = proj ? proj->findPage(key.pageId) : nullptr;
                                if (pg != nullptr) {
                                    prepReq.pageSize = QSize(pg->imageSize().width, pg->imageSize().height);
                                }
                                prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

                                prepareAdoptionAsync(key, prepReq, sessionGen, rootContext);
                            } else if (!batchState->failedRegions.isEmpty()) {
                                if (rootContext.progressReporter) {
                                    rootContext.progressReporter->markFinished(batchState->lastError);
                                }
                                if (batchState->lastError.category != application::ErrorCategory::Cancelled) {
                                    m_pageFailedRegions[key.pageId] = batchState->failedRegions;
                                    m_pageSafeMessages[key.pageId] = batchState->lastError.safeMessage;
                                    if (key.pageId == m_viewModel->state().currentPageId) {
                                        updateProcessingStateForCurrentPage();
                                    }
                                    Q_EMIT processingFinished(key.pageId, false);
                                }
                            }
                        }
                    }
                    watcher->deleteLater();
                });
        watcher->setFuture(future);
    }

    updateProcessingStateForCurrentPage();
    if (rootOpId.isValid()) {
        Q_EMIT processingStarted(pageId, rootOpId);
    }
}

void EditorProcessingController::clearRegion(const domain::PageId& pageId, const domain::RegionId& regionId) {
    clearRegions(pageId, {regionId});
}

void EditorProcessingController::layoutSelectedRegions() {
    if (m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    const auto selected = m_viewModel->state().selectedRegionIds;
    for (const auto& rId : selected) {
        layoutRegion(pageId, rId);
    }
}

void EditorProcessingController::layoutRegion(const domain::PageId& pageId, const domain::RegionId& regionId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || !regionId.isValid() ||
        m_layoutUseCase == nullptr || m_registry == nullptr) {
        return;
    }

    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return;
    }
    const auto* region = page->findRegion(regionId);
    if (region == nullptr) {
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, regionId, application::OperationStage::Layout};
    cancelMatchingTask(key);

    application::LayoutRequest request;
    request.pageId = pageId;
    request.regionId = regionId;
    request.text = QString::fromStdString(
        region->translation().effectiveText().empty() ? region->source().text : region->translation().effectiveText());
    if (request.text.isEmpty()) {
        request.text = QStringLiteral("Sample");
    }
    request.bounds = region->geometry();
    request.logicalDpi = 72.0;

    application::OperationDescriptor desc;
    desc.projectId = projectId;
    desc.pageId = pageId;
    desc.regionId = regionId;
    desc.stage = application::OperationStage::Layout;

    application::OperationId opId;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto future = m_layoutUseCase->execute(request, desc, sessionGen, opId);

    ActiveTask activeTask;
    activeTask.operationId = opId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = {regionId};
    activeTask.expectedRevisions.insert(regionId, region->contentRevision());
    m_activeTasks.insert(key, activeTask);

    auto* watcher = new QFutureWatcher<application::Result<application::LayoutResponse>>(this);
    connect(watcher,
            &QFutureWatcherBase::finished,
            this,
            [this, watcher, key, opId, sessionGen, regionId, rev = region->contentRevision()]() {
                if (m_viewModel != nullptr && m_viewModel->hasSession() &&
                    sessionGen == m_viewModel->projectSessionGeneration() && m_activeTasks.contains(key) &&
                    m_activeTasks.value(key).operationId == opId) {
                    m_activeTasks.remove(key);
                    auto res = watcher->result();
                    if (res.has_value()) {
                        auto layoutResp = res.value();
                        application::PageProcessingProposal prop;
                        prop.projectId = key.projectId;
                        prop.pageId = key.pageId;
                        prop.rootOperationId = opId;
                        prop.layout.append(layoutResp);
                        m_pageProposals[key.pageId] = prop;

                        application::PrepareProcessingAdoptionRequest prepReq;
                        prepReq.projectId = key.projectId;
                        prepReq.pageId = key.pageId;
                        prepReq.stage = application::OperationStage::Layout;
                        prepReq.sessionGeneration = sessionGen;
                        prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                        prepReq.layout.append(layoutResp);
                        prepReq.expectedRevisions.insert(regionId, rev);
                        prepReq.projectRoot = m_viewModel->projectRootPath();
                        prepReq.replaceManualPolicy = application::ReplaceManualPolicy::Keep;

                        prepareAdoptionAsync(key, prepReq, sessionGen);
                    } else {
                        const auto& err = res.error();
                        if (err.category != application::ErrorCategory::Cancelled) {
                            m_pageFailedRegions[key.pageId] = {regionId};
                            m_pageSafeMessages[key.pageId] = err.safeMessage;
                            if (key.pageId == m_viewModel->state().currentPageId) {
                                updateProcessingStateForCurrentPage();
                            }
                            Q_EMIT processingFinished(key.pageId, false);
                        }
                    }
                }
                watcher->deleteLater();
            });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
}

void EditorProcessingController::cancelOperation(application::OperationId operationId) {
    if (m_registry != nullptr) {
        m_registry->cancelOperation(operationId);
    }
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        if (it.value().operationId == operationId) {
            m_activeTasks.erase(it);
            break;
        }
    }
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::cancelPageOperations(const domain::PageId& pageId) {
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end();) {
        if (it.key().pageId == pageId) {
            if (m_registry != nullptr) {
                m_registry->cancelOperation(it.value().operationId);
            }
            it = m_activeTasks.erase(it);
        } else {
            ++it;
        }
    }
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::cancelAll() {
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        if (m_registry != nullptr) {
            m_registry->cancelOperation(it.value().operationId);
        }
    }
    m_activeTasks.clear();
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::retryFailedStage(const domain::PageId& pageId,
                                                  const domain::RegionId& regionId,
                                                  application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Ocr:
            recognizeRegion(pageId, regionId);
            break;
        case application::OperationStage::Translate:
            smartTranslatePage(pageId);
            break;
        case application::OperationStage::Clear:
            clearRegion(pageId, regionId);
            break;
        case application::OperationStage::Layout:
            layoutRegion(pageId, regionId);
            break;
        default:
            smartTranslatePage(pageId);
            break;
    }
}

void EditorProcessingController::adoptProposal(const domain::PageId& pageId) {
    if (m_viewModel == nullptr) {
        return;
    }

    if (m_preparedAdoptions.contains(pageId)) {
        const auto prepared = m_preparedAdoptions.value(pageId);
        const auto result = m_viewModel->adoptProcessingProposal(prepared);
        if (!result) {
            m_pageSafeMessages[pageId] = result.error().safeMessage;
            if (pageId == m_viewModel->state().currentPageId) {
                EditorNotice notice;
                notice.safeMessage = result.error().safeMessage;
                notice.retryable = true;
                m_viewModel->setNotice(notice);
                updateProcessingStateForCurrentPage();
            }
            return;
        }
        m_preparedAdoptions.remove(pageId);
        m_pageProposals.remove(pageId);
        m_pageConflictRegions.remove(pageId);
        m_pageSafeMessages.remove(pageId);
        updateProcessingStateForCurrentPage();
        Q_EMIT proposalAdopted(pageId);
    } else if (m_pageProposals.contains(pageId)) {
        m_pageSafeMessages[pageId] = tr("候选结果尚未准备好，请稍后重试");
        updateProcessingStateForCurrentPage();
    }
}

void EditorProcessingController::rejectProposal(const domain::PageId& pageId) {
    if (m_viewModel == nullptr) {
        return;
    }

    application::OperationId opId;
    if (m_pageProposals.contains(pageId)) {
        opId = m_pageProposals.value(pageId).rootOperationId;
        m_pageProposals.remove(pageId);
    }
    m_preparedAdoptions.remove(pageId);
    m_pageConflictRegions.remove(pageId);
    m_pageSafeMessages.remove(pageId);

    m_viewModel->rejectProcessingProposal(opId);
    updateProcessingStateForCurrentPage();
    Q_EMIT proposalRejected(pageId);
}

void EditorProcessingController::handlePageSwitched(const domain::PageId& pageId) {
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end();) {
        if (it.key().pageId != pageId && it.key().pageId.isValid()) {
            if (m_registry != nullptr)
                m_registry->cancelOperation(it.value().operationId);
            it = m_activeTasks.erase(it);
        } else {
            ++it;
        }
    }
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::reset() {
    cancelAll();
    releaseEngines();
    m_pageProposals.clear();
    m_preparedAdoptions.clear();
    m_pageConflictRegions.clear();
    m_pageSafeMessages.clear();
    m_pageFailedRegions.clear();
    if (m_viewModel != nullptr && m_viewModel->hasSession()) {
        EditorProcessingState empty;
        m_viewModel->updateProcessingState(empty);
    }
}

bool EditorProcessingController::hasActiveOperation(const domain::PageId& pageId) const {
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        if (it.key().pageId == pageId) {
            return true;
        }
    }
    return false;
}

bool EditorProcessingController::hasProposal(const domain::PageId& pageId) const {
    return m_pageProposals.contains(pageId) || m_preparedAdoptions.contains(pageId);
}

const application::PageProcessingProposal* EditorProcessingController::proposal(const domain::PageId& pageId) const {
    auto it = m_pageProposals.find(pageId);
    return it != m_pageProposals.end() ? &it.value() : nullptr;
}

const application::PreparedProcessingAdoption*
EditorProcessingController::preparedAdoption(const domain::PageId& pageId) const {
    auto it = m_preparedAdoptions.find(pageId);
    return it != m_preparedAdoptions.end() ? &it.value() : nullptr;
}

void EditorProcessingController::updateProcessingStateForCurrentPage() {
    if (m_viewModel == nullptr) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    if (!pageId.isValid()) {
        EditorProcessingState empty;
        m_viewModel->updateProcessingState(empty);
        return;
    }

    EditorProcessingState proc;
    proc.activePageId = pageId;

    bool running = false;
    for (auto it = m_activeTasks.begin(); it != m_activeTasks.end(); ++it) {
        if (it.key().pageId == pageId) {
            running = true;
            proc.isRunning = true;
            proc.status = ProcessingStatus::Running;
            proc.activeOperationId = it.value().operationId;
            proc.stage = it.key().stage;
            proc.targetRegionIds = it.value().targetRegionIds;
            proc.runningRegionIds = it.value().targetRegionIds;
            for (const auto& rId : it.value().targetRegionIds) {
                proc.regionStatuses.append(
                    RegionProcessingStatus{rId, ProcessingStatus::Running, it.key().stage, QString(), false});
            }
            break;
        }
    }

    if (!running) {
        if (hasProposal(pageId)) {
            proc.status = ProcessingStatus::NeedsReview;
            proc.hasProposal = true;
            if (m_pageProposals.contains(pageId)) {
                proc.activeOperationId = m_pageProposals.value(pageId).rootOperationId;
            }
            proc.conflictRegionIds = m_pageConflictRegions.value(pageId);
            proc.safeMessage = m_pageSafeMessages.value(pageId);

            const auto* page =
                m_viewModel->projectAggregate() ? m_viewModel->projectAggregate()->findPage(pageId) : nullptr;
            if (page != nullptr) {
                for (const auto& r : page->regions()) {
                    proc.needsReviewRegionIds.append(r.id());
                    proc.regionStatuses.append(RegionProcessingStatus{r.id(),
                                                                      ProcessingStatus::NeedsReview,
                                                                      application::OperationStage::Translate,
                                                                      QString(),
                                                                      proc.conflictRegionIds.contains(r.id())});
                }
            }
        } else if (m_pageFailedRegions.contains(pageId)) {
            proc.status = ProcessingStatus::Failed;
            proc.failedRegionIds = m_pageFailedRegions.value(pageId);
            proc.safeMessage = m_pageSafeMessages.value(pageId);
            for (const auto& rId : proc.failedRegionIds) {
                proc.regionStatuses.append(RegionProcessingStatus{
                    rId, ProcessingStatus::Failed, application::OperationStage::Translate, proc.safeMessage, false});
            }
        } else {
            proc.status = ProcessingStatus::Idle;
        }
    }

    m_viewModel->updateProcessingState(proc);
}

void EditorProcessingController::batchProcessPages(const QVector<domain::PageId>& pageIds,
                                                   application::BatchStageFlags stages) {
    if (pageIds.isEmpty() || m_batchWorkflow == nullptr || m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    auto* project = m_viewModel->projectAggregate();
    if (project == nullptr) {
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, domain::PageId{}, std::nullopt, application::OperationStage::Project};
    cancelMatchingTask(key);

    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::Project,
        .projectId = projectId,
        .pageId = domain::PageId{},
        .regionId = std::nullopt,
        .displayTarget = tr("全书批量翻译 (共 %1 页)").arg(pageIds.size()),
        .cancellable = true,
    };
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto context = m_registry != nullptr
                       ? m_registry->createOperation(std::move(descriptor), sessionGen, m_viewModel->viewGeneration())
                       : application::OperationContext{application::OperationId::generate(), sessionGen};
    const auto opId = context.operationId;

    ActiveTask task;
    task.operationId = opId;
    task.sessionGeneration = sessionGen;
    task.key = key;
    m_activeTasks[key] = task;

    application::BatchProcessingRequest req;
    req.projectId = projectId;
    req.pageIds = pageIds;
    req.stages = stages;
    req.project = project;
    req.sourceLanguage = QString::fromStdString(project->sourceLanguage().toString());
    req.targetLanguage = QString::fromStdString(project->targetLanguage().toString());
    req.readingDirection = project->readingDirection();
    req.projectRoot = m_viewModel->projectRootPath();

    auto future = m_batchWorkflow->execute(req, context, sessionGen);
    auto* watcher = new QFutureWatcher<application::Result<application::BatchProcessingReport>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, key, opId, sessionGen]() {
        const bool ownsTask = m_activeTasks.contains(key) && m_activeTasks[key].operationId == opId;
        if (ownsTask) {
            m_activeTasks.remove(key);
        }
        if (m_registry != nullptr) {
            m_registry->markFinished(opId);
        }
        if (ownsTask && m_viewModel != nullptr && m_viewModel->hasSession() &&
            sessionGen == m_viewModel->projectSessionGeneration()) {
            auto result = watcher->result();
            if (result.has_value() && result->projectSaved) {
                m_viewModel->markProjectCleanAtRevision(result->savedRevision);
            }
            const bool success = result.has_value() && result->failedPages == 0;
            Q_EMIT processingFinished(domain::PageId{}, success);
        }
        watcher->deleteLater();
        updateProcessingStateForCurrentPage();
    });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(domain::PageId{}, opId);
}

void EditorProcessingController::batchDetectPages(const QVector<domain::PageId>& pageIds) {
    batchProcessPages(pageIds, application::BatchStageFlag::Detect);
}

void EditorProcessingController::batchOcrPages(const QVector<domain::PageId>& pageIds) {
    batchProcessPages(pageIds, application::BatchStageFlag::Ocr);
}

void EditorProcessingController::batchTranslatePages(const QVector<domain::PageId>& pageIds) {
    batchProcessPages(pageIds, application::BatchStageFlag::Translate);
}

void EditorProcessingController::batchClearPages(const QVector<domain::PageId>& pageIds) {
    batchProcessPages(pageIds, application::BatchStageFlag::Clear);
}

void EditorProcessingController::batchSmartTranslatePages(const QVector<domain::PageId>& pageIds) {
    batchProcessPages(pageIds, application::BatchStageFlag::All);
}

} // namespace manga::ui::screen::editor
