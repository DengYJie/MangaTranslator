#include "ui/screen/editor/EditorProcessingController.h"

#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "application/workflow/SmartTranslatePageWorkflow.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/TextContent.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QCoreApplication>

namespace manga::ui::screen::editor {

EditorProcessingController::EditorProcessingController(EditorViewModel* viewModel,
                                                       application::IOcrEngine* ocrEngine,
                                                       application::ITranslator* translator,
                                                       application::IInpainter* inpainter,
                                                       application::ITextLayouter* layouter,
                                                       application::IDerivedAssetStore* assetStore,
                                                       application::OperationRegistry* registry,
                                                       QObject* parent)
    : QObject(parent), m_viewModel(viewModel), m_registry(registry), m_assetStore(assetStore) {
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
    QObject* parent)
    : QObject(parent), m_viewModel(viewModel), m_registry(registry), m_ocrUseCase(ocrUseCase),
      m_translateUseCase(translateUseCase), m_clearUseCase(clearUseCase), m_layoutUseCase(layoutUseCase),
      m_smartWorkflow(smartWorkflow), m_prepareAdoptionUseCase(prepareAdoptionUseCase) {
    if (m_viewModel != nullptr) {
        m_viewModel->setProcessingController(this);
    }
}

EditorProcessingController::~EditorProcessingController() {
    reset();
    if (m_viewModel != nullptr && m_viewModel->processingController() == this) {
        m_viewModel->setProcessingController(nullptr);
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

    for (const auto& region : page->regions()) {
        application::RegionProcessingTarget target;
        target.regionId = region.id();
        target.geometry = region.geometry();
        target.sourceText = QString::fromStdString(region.source().text);
        target.effectiveTranslation = QString::fromStdString(region.translation().effectiveText());
        target.contentRevision = region.contentRevision();
        target.hasManualTranslation = region.translation().hasManualText();
        targets.append(target);

        expectedRevisions.insert(region.id(), region.contentRevision());
        targetRegionIds.append(region.id());
    }

    const QString sourceLang = QStringLiteral("ja");
    const QString targetLang = QStringLiteral("en");

    application::OperationId opId;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    QImage dummyImage(page->imageSize().width > 0 ? page->imageSize().width : 800,
                      page->imageSize().height > 0 ? page->imageSize().height : 1200,
                      QImage::Format_RGB32);
    dummyImage.fill(Qt::white);

    auto future =
        m_smartWorkflow->execute(projectId, pageId, targets, sourceLang, targetLang, dummyImage, sessionGen, opId);

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

    // Prepare adoption
    if (project != nullptr) {
        application::PrepareClearResultUseCase clearUseCase(project, m_assetStore);
        application::PrepareProcessingAdoptionUseCase prepareUseCase(project, &clearUseCase);

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

        auto prepFuture = prepareUseCase.execute(prepReq);
        if (prepFuture.isFinished() && prepFuture.result().has_value()) {
            m_preparedAdoptions[key.pageId] = prepFuture.result().value();
        }
    }

    if (key.pageId == m_viewModel->state().currentPageId) {
        updateProcessingStateForCurrentPage();
    }

    Q_EMIT proposalReady(key.pageId);
    Q_EMIT processingFinished(key.pageId, true);
}

void EditorProcessingController::recognizeSelectedRegions() {
    if (m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    const auto selected = m_viewModel->state().selectedRegionIds;
    for (const auto& rId : selected) {
        recognizeRegion(pageId, rId);
    }
}

void EditorProcessingController::recognizeRegion(const domain::PageId& pageId, const domain::RegionId& regionId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || !regionId.isValid() ||
        m_ocrUseCase == nullptr || m_registry == nullptr) {
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
    request.language = domain::languages::Japanese;
    request.image = QImage(region->geometry().width > 0 ? static_cast<int>(region->geometry().width) : 100,
                           region->geometry().height > 0 ? static_cast<int>(region->geometry().height) : 100,
                           QImage::Format_RGB32);
    request.image.fill(Qt::white);

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
                if (m_viewModel != nullptr && m_viewModel->hasSession() &&
                    sessionGen == m_viewModel->projectSessionGeneration() && m_activeTasks.contains(key) &&
                    m_activeTasks.value(key).operationId == opId) {
                    m_activeTasks.remove(key);
                    auto res = watcher->result();
                    if (res.has_value()) {
                        auto ocrResp = res.value();
                        application::PageProcessingProposal prop;
                        prop.projectId = key.projectId;
                        prop.pageId = key.pageId;
                        prop.rootOperationId = opId;
                        prop.ocr.append(ocrResp);
                        m_pageProposals[key.pageId] = prop;

                        // Prepare adoption
                        auto* proj = m_viewModel->projectAggregate();
                        if (proj != nullptr) {
                            application::PrepareClearResultUseCase clearUseCase(proj, m_assetStore);
                            application::PrepareProcessingAdoptionUseCase prepareUseCase(proj, &clearUseCase);
                            application::PrepareProcessingAdoptionRequest prepReq;
                            prepReq.projectId = key.projectId;
                            prepReq.pageId = key.pageId;
                            prepReq.stage = application::OperationStage::Ocr;
                            prepReq.sessionGeneration = sessionGen;
                            prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                            prepReq.ocr.append(ocrResp);
                            prepReq.expectedRevisions.insert(regionId, rev);
                            auto prepFuture = prepareUseCase.execute(prepReq);
                            if (prepFuture.isFinished() && prepFuture.result().has_value()) {
                                m_preparedAdoptions[key.pageId] = prepFuture.result().value();
                            }
                        }
                        if (key.pageId == m_viewModel->state().currentPageId) {
                            updateProcessingStateForCurrentPage();
                        }
                        Q_EMIT proposalReady(key.pageId);
                        Q_EMIT processingFinished(key.pageId, true);
                    } else {
                        m_pageFailedRegions[key.pageId] = {regionId};
                        m_pageSafeMessages[key.pageId] = res.error().safeMessage;
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

void EditorProcessingController::translatePage(const domain::PageId& pageId) {
    smartTranslatePage(pageId);
}

void EditorProcessingController::clearSelectedRegions() {
    if (m_viewModel == nullptr || !m_viewModel->hasSession()) {
        return;
    }
    const auto pageId = m_viewModel->state().currentPageId;
    const auto selected = m_viewModel->state().selectedRegionIds;
    for (const auto& rId : selected) {
        clearRegion(pageId, rId);
    }
}

void EditorProcessingController::clearRegion(const domain::PageId& pageId, const domain::RegionId& regionId) {
    if (m_viewModel == nullptr || !m_viewModel->hasSession() || !pageId.isValid() || !regionId.isValid() ||
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
    const auto* region = page->findRegion(regionId);
    if (region == nullptr) {
        return;
    }

    const auto projectId = project->id();
    const WatcherKey key{projectId, pageId, regionId, application::OperationStage::Clear};
    cancelMatchingTask(key);

    application::ClearRequest request;
    request.projectId = projectId;
    request.pageId = pageId;
    request.regionId = regionId;
    request.region = region->geometry();
    request.settings.enabled = true;
    request.pageImage = QImage(page->imageSize().width > 0 ? page->imageSize().width : 800,
                               page->imageSize().height > 0 ? page->imageSize().height : 1200,
                               QImage::Format_RGB32);
    request.pageImage.fill(Qt::white);

    application::OperationDescriptor desc;
    desc.projectId = projectId;
    desc.pageId = pageId;
    desc.regionId = regionId;
    desc.stage = application::OperationStage::Clear;

    application::OperationId opId;
    const quint64 sessionGen = m_viewModel->projectSessionGeneration();
    auto future = m_clearUseCase->execute(request, desc, sessionGen, opId);

    ActiveTask activeTask;
    activeTask.operationId = opId;
    activeTask.sessionGeneration = sessionGen;
    activeTask.key = key;
    activeTask.targetRegionIds = {regionId};
    activeTask.expectedRevisions.insert(regionId, region->contentRevision());
    m_activeTasks.insert(key, activeTask);

    auto* watcher = new QFutureWatcher<application::Result<application::ClearResponse>>(this);
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
                        auto clearResp = res.value();
                        application::PageProcessingProposal prop;
                        prop.projectId = key.projectId;
                        prop.pageId = key.pageId;
                        prop.rootOperationId = opId;
                        prop.clear.append(clearResp);
                        m_pageProposals[key.pageId] = prop;

                        // Prepare adoption
                        auto* proj = m_viewModel->projectAggregate();
                        if (proj != nullptr) {
                            application::PrepareClearResultUseCase clearUseCase(proj, m_assetStore);
                            application::PrepareProcessingAdoptionUseCase prepareUseCase(proj, &clearUseCase);
                            application::PrepareProcessingAdoptionRequest prepReq;
                            prepReq.projectId = key.projectId;
                            prepReq.pageId = key.pageId;
                            prepReq.stage = application::OperationStage::Clear;
                            prepReq.sessionGeneration = sessionGen;
                            prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                            prepReq.clear.append(clearResp);
                            prepReq.expectedRevisions.insert(regionId, rev);
                            prepReq.projectRoot = m_viewModel->projectRootPath();
                            const auto* pg = proj->findPage(key.pageId);
                            if (pg != nullptr) {
                                prepReq.pageSize = QSize(pg->imageSize().width, pg->imageSize().height);
                            }
                            auto prepFuture = prepareUseCase.execute(prepReq);
                            if (prepFuture.isFinished() && prepFuture.result().has_value()) {
                                m_preparedAdoptions[key.pageId] = prepFuture.result().value();
                            }
                        }
                        if (key.pageId == m_viewModel->state().currentPageId) {
                            updateProcessingStateForCurrentPage();
                        }
                        Q_EMIT proposalReady(key.pageId);
                        Q_EMIT processingFinished(key.pageId, true);
                    }
                }
                watcher->deleteLater();
            });
    watcher->setFuture(future);

    updateProcessingStateForCurrentPage();
    Q_EMIT processingStarted(pageId, opId);
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
    request.logicalDpi = 96.0;

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

                        // Prepare adoption
                        auto* proj = m_viewModel->projectAggregate();
                        if (proj != nullptr) {
                            application::PrepareClearResultUseCase clearUseCase(proj, m_assetStore);
                            application::PrepareProcessingAdoptionUseCase prepareUseCase(proj, &clearUseCase);
                            application::PrepareProcessingAdoptionRequest prepReq;
                            prepReq.projectId = key.projectId;
                            prepReq.pageId = key.pageId;
                            prepReq.stage = application::OperationStage::Layout;
                            prepReq.sessionGeneration = sessionGen;
                            prepReq.currentSessionGeneration = m_viewModel->projectSessionGeneration();
                            prepReq.layout.append(layoutResp);
                            prepReq.expectedRevisions.insert(regionId, rev);
                            auto prepFuture = prepareUseCase.execute(prepReq);
                            if (prepFuture.isFinished() && prepFuture.result().has_value()) {
                                m_preparedAdoptions[key.pageId] = prepFuture.result().value();
                            }
                        }
                        if (key.pageId == m_viewModel->state().currentPageId) {
                            updateProcessingStateForCurrentPage();
                        }
                        Q_EMIT proposalReady(key.pageId);
                        Q_EMIT processingFinished(key.pageId, true);
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
        auto prepared = m_preparedAdoptions.take(pageId);
        m_pageProposals.remove(pageId);
        m_pageConflictRegions.remove(pageId);
        m_pageSafeMessages.remove(pageId);
        [[maybe_unused]] auto result = m_viewModel->adoptProcessingProposal(prepared);
        updateProcessingStateForCurrentPage();
        Q_EMIT proposalAdopted(pageId);
    } else if (m_pageProposals.contains(pageId)) {
        m_pageProposals.remove(pageId);
        m_pageConflictRegions.remove(pageId);
        m_pageSafeMessages.remove(pageId);
        updateProcessingStateForCurrentPage();
        Q_EMIT proposalAdopted(pageId);
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

void EditorProcessingController::handlePageSwitched(const domain::PageId&) {
    updateProcessingStateForCurrentPage();
}

void EditorProcessingController::reset() {
    cancelAll();
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

} // namespace manga::ui::screen::editor
