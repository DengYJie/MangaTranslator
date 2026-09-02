#include "application/workflow/BatchPagesProcessingWorkflow.h"

#include "application/dto/ClearDto.h"
#include "application/dto/LayoutDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/ProjectDto.h"
#include "application/dto/TextDetectionDto.h"
#include "application/dto/TranslationDto.h"
#include "application/ports/IDerivedAssetStore.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IProjectRepository.h"
#include "application/ports/ITextDetector.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "application/workflow/SmartTranslatePageWorkflow.h"

#include <QCoreApplication>
#include <QFuture>
#include <QImage>
#include <QPromise>
#include <QSet>
#include <QSharedPointer>

#include <algorithm>
#include <memory>
#include <utility>

namespace manga::application {

namespace {

constexpr qint64 kStepsPerPage = 7;

} // namespace

class BatchOrchestrator : public std::enable_shared_from_this<BatchOrchestrator> {
public:
    BatchOrchestrator(BatchPagesProcessingWorkflow* workflow,
                      RecognizeRegionUseCase* ocrUseCase,
                      TranslatePageUseCase* translateUseCase,
                      ClearRegionUseCase* clearUseCase,
                      LayoutRegionUseCase* layoutUseCase,
                      ITextDetector* detector,
                      IPageImageLoader* pageImageLoader,
                      IDerivedAssetStore* derivedAssetStore,
                      IProjectRepository* projectRepository,
                      OperationRegistry* registry)
        : m_workflow(workflow), m_ocrUseCase(ocrUseCase), m_translateUseCase(translateUseCase),
          m_clearUseCase(clearUseCase), m_layoutUseCase(layoutUseCase), m_detector(detector),
          m_pageImageLoader(pageImageLoader), m_derivedAssetStore(derivedAssetStore),
          m_projectRepository(projectRepository), m_registry(registry) {
        m_promise = std::make_shared<QPromise<Result<BatchProcessingReport>>>();
    }

    [[nodiscard]] QFuture<Result<BatchProcessingReport>>
    start(BatchProcessingRequest request, OperationContext context, quint64 sessionGeneration) {
        m_request = std::move(request);
        m_context = std::move(context);
        m_sessionGeneration = sessionGeneration;

        m_report.projectId = m_request.projectId;
        m_report.totalPages = 0;
        m_report.completedPages = 0;
        m_report.failedPages = 0;

        m_promise->start();
        auto future = m_promise->future();

        resolvePageList();

        m_report.totalPages = static_cast<int>(m_pageIds.size());
        if (m_pageIds.isEmpty()) {
            if (m_context.progressReporter) {
                m_context.progressReporter->setRange(1);
                m_context.progressReporter->setValue(1);
                m_context.progressReporter->setStatusText(QObject::tr("没有需要处理的页面"));
                m_context.progressReporter->markFinished();
            } else if (m_registry != nullptr && m_context.operationId.isValid()) {
                m_registry->markFinished(m_context.operationId);
            }
            finish(m_report);
            return future;
        }

        m_totalSteps = std::max<qint64>(1, static_cast<qint64>(m_pageIds.size() * kStepsPerPage));
        if (m_context.progressReporter) {
            m_context.progressReporter->setRange(m_totalSteps);
            m_context.progressReporter->setValue(0);
            m_context.progressReporter->setStatusText(
                QObject::tr("第 1/%1 页 · %2").arg(m_pageIds.size()).arg(QObject::tr("准备处理")));
            m_context.progressReporter->markRunning();
        } else if (m_registry != nullptr && m_context.operationId.isValid()) {
            m_registry->markRunning(m_context.operationId);
        }

        runPage(0);
        return future;
    }

private:
    void resolvePageList() {
        if (!m_request.pageIds.isEmpty()) {
            m_pageIds = m_request.pageIds;
            return;
        }

        if (m_request.project == nullptr) {
            return;
        }

        const auto& allPages = m_request.project->pages();
        for (const auto& p : allPages) {
            switch (m_request.scope) {
                case BatchScopeType::AllPages:
                    m_pageIds.append(p.id());
                    break;
                case BatchScopeType::SelectedPages:
                    // An empty selection resolves to no work. It never falls back to the project.
                    break;
                case BatchScopeType::Unprocessed:
                case BatchScopeType::FailedOnly:
                    // No per-run failure history reaches the workflow, so a retry reprocesses the
                    // pages still missing detection output or a committed clear layer.
                    if (p.regions().empty() || !p.clearLayerAsset().has_value()) {
                        m_pageIds.append(p.id());
                    }
                    break;
            }
        }
    }

    /// Ports complete their promise on a worker thread, so a QFuture::then body inherits that
    /// affinity. Every mutation of orchestrator or project state is therefore marshalled here first.
    template <typename F> void postStep(F&& func) {
        if (qApp != nullptr) {
            QMetaObject::invokeMethod(qApp, std::forward<F>(func), Qt::QueuedConnection);
        } else {
            func();
        }
    }

    [[nodiscard]] OperationContext workerContext() const {
        auto context = m_context;
        // The composite workflow owns root progress. Child adapters must not overwrite its range
        // or settle it while they report their own local progress.
        context.progressReporter.reset();
        return context;
    }

    void updateProgress(int stepWithinPage, const QString& stageName) {
        const int displayPage = m_currentPageIndex + 1;
        const int total = static_cast<int>(m_pageIds.size());
        if (m_context.progressReporter) {
            const qint64 currentStep = m_currentPageIndex * kStepsPerPage + stepWithinPage;
            m_context.progressReporter->setValue(currentStep);
            m_context.progressReporter->setStatusText(
                QObject::tr("第 %1/%2 页 · %3").arg(displayPage).arg(total).arg(stageName));
        }
    }

    void finish(const Result<BatchProcessingReport>& result) {
        if (m_settled) {
            return;
        }
        m_settled = true;

        m_promise->addResult(result);
        m_promise->finish();

        if (m_context.progressReporter) {
            if (result.has_value()) {
                m_context.progressReporter->setValue(m_totalSteps);
                m_context.progressReporter->setStatusText(QObject::tr("批量处理完成"));
                m_context.progressReporter->markFinished();
            } else {
                m_context.progressReporter->markFinished(result.error());
            }
        } else if (m_registry != nullptr && m_context.operationId.isValid()) {
            if (result.has_value()) {
                m_registry->markFinished(m_context.operationId);
            } else {
                m_registry->markFinished(m_context.operationId, result.error());
            }
        }
    }

    void finishCancelled() {
        finishWithSave(std::optional<ApplicationError>{makeApplicationError(
            ErrorCategory::Cancelled, QStringLiteral("cancelled.user_requested"), QStringLiteral("批量处理已被取消"))});
    }

    // Derived page assets are content-addressed files already written per page, so only the
    // project metadata is deferred to one batch-end write.
    void finishWithSave(const std::optional<ApplicationError>& abortError) {
        const auto abortOrFinish = [this, abortError]() {
            if (abortError.has_value()) {
                finish(std::unexpected(*abortError));
            } else {
                finish(m_report);
            }
        };

        if (!m_projectDirty || m_projectRepository == nullptr || m_request.project == nullptr) {
            abortOrFinish();
            return;
        }

        m_projectDirty = false;
        if (m_context.progressReporter) {
            m_context.progressReporter->setStatusText(QObject::tr("保存项目"));
        }

        SaveProjectRequest saveReq;
        saveReq.project = *m_request.project;
        auto saveFuture = m_projectRepository->saveProject(saveReq, workerContext());
        auto self = shared_from_this();
        saveFuture.then([this, self, abortError, abortOrFinish](Result<SaveProjectResult> saveRes) {
            postStep([this, self, abortError, abortOrFinish, saveRes]() {
                if (!saveRes.has_value()) {
                    if (abortError.has_value()) {
                        abortOrFinish();
                    } else {
                        finish(std::unexpected(saveRes.error()));
                    }
                    return;
                }
                m_report.projectSaved = true;
                m_report.savedRevision = saveRes->savedRevision;
                abortOrFinish();
            });
        });
    }

    // Records the first failing stage for the page in flight. The page is still committed so that
    // successful regions keep their results, but it reports as failed rather than as a clean run.
    void noteStageFailure(const QString& reason) {
        if (m_currentPageFailure.isEmpty()) {
            m_currentPageFailure = reason;
        }
    }

    void runPage(int pageIndex) {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (pageIndex >= m_pageIds.size()) {
            finishWithSave(std::nullopt);
            return;
        }

        m_currentPageIndex = pageIndex;
        const auto& pageId = m_pageIds[pageIndex];
        m_currentPageSummary = BatchPageSummary{.pageId = pageId, .success = false, .failureReason = {}};

        m_currentPageImage = QImage();
        m_currentPageTargets.clear();
        m_currentOcr.clear();
        m_currentTranslation.reset();
        m_currentClear.clear();
        m_currentLayout.clear();
        m_currentClearFailedRegions.clear();
        m_currentPageFailure.clear();

        runLoadImageStep();
    }

    void completeCurrentPage(bool success, const QString& failureReason) {
        const auto& pageId = m_pageIds[m_currentPageIndex];
        m_currentPageSummary.pageId = pageId;
        m_currentPageSummary.success = success;
        m_currentPageSummary.failureReason = failureReason;

        m_report.pageSummaries.append(m_currentPageSummary);
        if (success) {
            m_report.completedPages++;
            if (m_workflow != nullptr) {
                Q_EMIT m_workflow->pageCommitted(pageId);
            }
        } else {
            m_report.failedPages++;
        }

        const int nextPageIndex = m_currentPageIndex + 1;
        postStep([this, self = shared_from_this(), nextPageIndex]() { runPage(nextPageIndex); });
    }

    void runLoadImageStep() {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        updateProgress(0, QObject::tr("加载图像"));

        if (m_request.project == nullptr) {
            completeCurrentPage(false, QObject::tr("项目未加载"));
            return;
        }

        const auto* page = m_request.project->findPage(m_pageIds[m_currentPageIndex]);
        if (page == nullptr) {
            completeCurrentPage(false, QObject::tr("找不到页面"));
            return;
        }

        if (m_pageImageLoader == nullptr) {
            completeCurrentPage(false, QObject::tr("页面图像加载服务不可用"));
            return;
        }

        LoadPageImageRequest loadReq;
        loadReq.projectId = m_request.projectId;
        loadReq.projectRoot = m_request.projectRoot;
        loadReq.pageId = page->id();
        loadReq.assetPath = QString::fromStdString(page->originalAssetPath());
        loadReq.expectedSize = page->imageSize();

        auto future = m_pageImageLoader->loadPageImage(loadReq, workerContext());
        auto self = shared_from_this();
        future.then([this, self](Result<QImage> res) {
            postStep([this, self, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (!res.has_value()) {
                    completeCurrentPage(false, res.error().safeMessage);
                    return;
                }
                m_currentPageImage = res.value();
                runDetectStep();
            });
        });
    }

    void runDetectStep() {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (!m_request.stages.testFlag(BatchStageFlag::Detect)) {
            postStep([this, self = shared_from_this()]() { prepareTargetsAndRunOcr(); });
            return;
        }

        updateProgress(1, QObject::tr("检测文字区域"));

        if (m_detector == nullptr) {
            completeCurrentPage(false, QObject::tr("检测服务不可用"));
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        TextDetectionRequest req;
        req.pageId = pageId;
        req.image = m_currentPageImage;
        req.readingDirection = m_request.project ? m_request.project->readingDirection() : m_request.readingDirection;
        req.minConfidence = m_request.minConfidence;

        auto future = m_detector->detect(std::move(req), workerContext());
        auto self = shared_from_this();
        future.then([this, self, pageId](Result<TextDetectionResponse> res) {
            postStep([this, self, pageId, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (!res.has_value()) {
                    completeCurrentPage(false, res.error().safeMessage);
                    return;
                }

                if (m_request.project != nullptr) {
                    const auto* page = m_request.project->findPage(pageId);
                    if (page != nullptr) {
                        if (m_request.replacePolicy == ReplaceManualPolicy::Replace) {
                            const auto existingRegions = page->regions();
                            for (const auto& r : existingRegions) {
                                if (!r.isLocked()) {
                                    const auto removed = m_request.project->removeRegionFromPage(pageId, r.id());
                                    if (!removed.has_value()) {
                                        noteStageFailure(QString::fromStdString(removed.error().toString()));
                                    }
                                }
                            }
                        }
                        const auto* freshPage = m_request.project->findPage(pageId);
                        int orderIndex = freshPage ? static_cast<int>(freshPage->regions().size()) : 0;
                        for (const auto& item : res->detectedRegions) {
                            if (!item.rect.isValid()) {
                                continue;
                            }
                            domain::TextRegion region(domain::RegionId::generate(),
                                                      item.rect,
                                                      item.bubbleRect,
                                                      orderIndex++,
                                                      item.bubblePolygon);
                            const auto added = m_request.project->addRegionToPage(pageId, std::move(region));
                            if (!added.has_value()) {
                                noteStageFailure(QString::fromStdString(added.error().toString()));
                            }
                        }
                    }
                }

                prepareTargetsAndRunOcr();
            });
        });
    }

    void prepareTargetsAndRunOcr() {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        const auto* page = m_request.project ? m_request.project->findPage(pageId) : nullptr;
        if (page == nullptr) {
            completeCurrentPage(false, QObject::tr("找不到页面"));
            return;
        }

        m_currentPageTargets.clear();
        for (const auto& region : page->regions()) {
            RegionProcessingTarget target;
            target.regionId = region.id();
            target.geometry = region.geometry();
            target.bubbleGeometry = region.bubbleGeometry();
            target.bubblePolygon = region.bubblePolygon();
            target.sourceText = QString::fromStdString(region.source().text);
            target.effectiveTranslation = QString::fromStdString(region.translation().effectiveText());
            target.contentRevision = region.contentRevision();
            target.hasManualTranslation = region.translation().hasManualText();
            m_currentPageTargets.append(target);
        }

        if (m_currentPageTargets.isEmpty()) {
            if (!m_request.stages.testFlag(BatchStageFlag::Detect)) {
                completeCurrentPage(false, QObject::tr("页面无有效文字区域，请先运行文本检测"));
            } else {
                postStep([this, self = shared_from_this()]() { runCommitStep(); });
            }
            return;
        }

        if (m_request.stages.testFlag(BatchStageFlag::Ocr)) {
            updateProgress(2, QObject::tr("识别文字"));
            runOcrForTarget(0);
        } else {
            postStep([this, self = shared_from_this()]() { runTranslateStep(); });
        }
    }

    void runOcrForTarget(int targetIndex) {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (targetIndex >= m_currentPageTargets.size()) {
            postStep([this, self = shared_from_this()]() { runTranslateStep(); });
            return;
        }

        auto& target = m_currentPageTargets[targetIndex];
        if (target.hasManualTranslation && m_request.replacePolicy != ReplaceManualPolicy::Replace) {
            postStep([this, self = shared_from_this(), targetIndex]() { runOcrForTarget(targetIndex + 1); });
            return;
        }

        if (m_ocrUseCase == nullptr) {
            completeCurrentPage(false, QObject::tr("OCR 服务不可用"));
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        OcrRequest ocrReq;
        ocrReq.pageId = pageId;
        ocrReq.regionId = target.regionId;
        ocrReq.image = m_currentPageImage;
        ocrReq.rect = target.geometry;
        ocrReq.language = domain::LanguageCode::createUnchecked(
            !m_request.sourceLanguage.isEmpty()
                ? m_request.sourceLanguage.toStdString()
                : (m_request.project ? m_request.project->sourceLanguage().toString() : "ja"));

        auto future = m_ocrUseCase->execute(ocrReq, workerContext());
        auto self = shared_from_this();
        future.then([this, self, targetIndex](Result<OcrResponse> res) {
            postStep([this, self, targetIndex, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (res.has_value()) {
                    m_currentOcr.append(res.value());
                    m_currentPageTargets[targetIndex].sourceText = res.value().text;
                } else {
                    noteStageFailure(res.error().safeMessage);
                }
                runOcrForTarget(targetIndex + 1);
            });
        });
    }

    void runTranslateStep() {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (!m_request.stages.testFlag(BatchStageFlag::Translate)) {
            postStep([this, self = shared_from_this()]() { runClearForTarget(0); });
            return;
        }

        updateProgress(3, QObject::tr("智能翻译"));

        TranslationRequest transReq;
        transReq.sourceLanguage = domain::LanguageCode::createUnchecked(
            !m_request.sourceLanguage.isEmpty()
                ? m_request.sourceLanguage.toStdString()
                : (m_request.project ? m_request.project->sourceLanguage().toString() : "ja"));
        transReq.targetLanguage = domain::LanguageCode::createUnchecked(
            !m_request.targetLanguage.isEmpty()
                ? m_request.targetLanguage.toStdString()
                : (m_request.project ? m_request.project->targetLanguage().toString() : "zh-Hans"));
        transReq.readingDirection =
            m_request.project ? m_request.project->readingDirection() : m_request.readingDirection;
        if (m_request.project != nullptr) {
            transReq.glossary = QVector<domain::GlossaryEntry>(m_request.project->glossary().begin(),
                                                               m_request.project->glossary().end());
        }

        for (const auto& t : m_currentPageTargets) {
            if (t.hasManualTranslation && m_request.replacePolicy != ReplaceManualPolicy::Replace) {
                continue;
            }
            if (!t.sourceText.trimmed().isEmpty()) {
                transReq.regions.append({t.regionId, t.sourceText});
            }
        }

        if (transReq.regions.isEmpty()) {
            if (!m_request.stages.testFlag(BatchStageFlag::Ocr)) {
                completeCurrentPage(false, QObject::tr("当前页面无识别文字，请先运行文字识别"));
                return;
            }
            postStep([this, self = shared_from_this()]() { runClearForTarget(0); });
            return;
        }

        if (m_translateUseCase == nullptr) {
            completeCurrentPage(false, QObject::tr("翻译服务不可用"));
            return;
        }

        auto future = m_translateUseCase->execute(transReq, workerContext());
        auto self = shared_from_this();
        future.then([this, self](Result<TranslationResponse> res) {
            postStep([this, self, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (res.has_value()) {
                    m_currentTranslation = res.value();
                    for (const auto& item : res.value().items) {
                        for (auto& t : m_currentPageTargets) {
                            if (t.regionId == item.regionId) {
                                t.effectiveTranslation = item.translation;
                            }
                        }
                    }
                } else {
                    noteStageFailure(res.error().safeMessage);
                }
                runClearForTarget(0);
            });
        });
    }

    void runClearForTarget(int targetIndex) {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (!m_request.stages.testFlag(BatchStageFlag::Clear)) {
            postStep([this, self = shared_from_this()]() { runLayoutForTarget(0); });
            return;
        }

        if (targetIndex == 0) {
            updateProgress(4, QObject::tr("清除背景"));
        }

        if (targetIndex >= m_currentPageTargets.size()) {
            postStep([this, self = shared_from_this()]() { runLayoutForTarget(0); });
            return;
        }

        auto& target = m_currentPageTargets[targetIndex];
        if (target.effectiveTranslation.isEmpty() && target.sourceText.isEmpty()) {
            postStep([this, self = shared_from_this(), targetIndex]() { runClearForTarget(targetIndex + 1); });
            return;
        }

        if (m_clearUseCase == nullptr) {
            completeCurrentPage(false, QObject::tr("清除服务不可用"));
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        ClearRequest clearReq;
        clearReq.projectId = m_request.projectId;
        clearReq.pageId = pageId;
        clearReq.regionId = target.regionId;
        clearReq.pageImage = m_currentPageImage;
        clearReq.region = target.geometry;
        clearReq.polygon = target.bubblePolygon;
        clearReq.settings.enabled = true;
        clearReq.contentRevision = target.contentRevision;

        auto future = m_clearUseCase->execute(clearReq, workerContext());
        auto self = shared_from_this();
        future.then([this, self, targetIndex, target](Result<ClearResponse> res) {
            postStep([this, self, targetIndex, target, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (res.has_value()) {
                    m_currentClear.append(res.value());
                } else {
                    m_currentClearFailedRegions.insert(target.regionId);
                    noteStageFailure(res.error().safeMessage);
                }
                runClearForTarget(targetIndex + 1);
            });
        });
    }

    void runLayoutForTarget(int targetIndex) {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        if (!m_request.stages.testFlag(BatchStageFlag::Layout)) {
            postStep([this, self = shared_from_this()]() { runCommitStep(); });
            return;
        }

        if (targetIndex == 0) {
            updateProgress(5, QObject::tr("排版文字"));
        }

        if (targetIndex >= m_currentPageTargets.size()) {
            postStep([this, self = shared_from_this()]() { runCommitStep(); });
            return;
        }

        auto& target = m_currentPageTargets[targetIndex];
        if (target.effectiveTranslation.isEmpty()) {
            postStep([this, self = shared_from_this(), targetIndex]() { runLayoutForTarget(targetIndex + 1); });
            return;
        }

        if (m_currentClearFailedRegions.contains(target.regionId)) {
            postStep([this, self = shared_from_this(), targetIndex]() { runLayoutForTarget(targetIndex + 1); });
            return;
        }

        if (m_layoutUseCase == nullptr) {
            completeCurrentPage(false, QObject::tr("排版服务不可用"));
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        LayoutRequest layoutReq;
        layoutReq.pageId = pageId;
        layoutReq.regionId = target.regionId;
        layoutReq.text = target.effectiveTranslation;
        layoutReq.bounds = target.geometry;
        layoutReq.constraints = domain::TextLayout{};
        layoutReq.logicalDpi = 72.0;
        layoutReq.contentRevision = target.contentRevision;

        auto future = m_layoutUseCase->execute(layoutReq, workerContext());
        auto self = shared_from_this();
        future.then([this, self, targetIndex](Result<LayoutResponse> res) {
            postStep([this, self, targetIndex, res]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (res.has_value()) {
                    m_currentLayout.append(res.value());
                } else {
                    noteStageFailure(res.error().safeMessage);
                }
                runLayoutForTarget(targetIndex + 1);
            });
        });
    }

    void runCommitStep() {
        if (m_context.stopToken.stop_requested()) {
            finishCancelled();
            return;
        }

        updateProgress(6, QObject::tr("提交页面结果"));

        if (m_request.project == nullptr) {
            completeCurrentPage(false, QObject::tr("项目不可用"));
            return;
        }

        const auto& pageId = m_pageIds[m_currentPageIndex];
        const auto* page = m_request.project->findPage(pageId);
        if (page == nullptr) {
            completeCurrentPage(false, QObject::tr("找不到页面"));
            return;
        }

        PrepareProcessingAdoptionRequest prepReq;
        prepReq.projectId = m_request.projectId;
        prepReq.pageId = pageId;
        prepReq.stage = OperationStage::Translate;
        prepReq.sessionGeneration = m_sessionGeneration;
        prepReq.currentSessionGeneration = m_sessionGeneration;
        prepReq.ocr = m_currentOcr;
        prepReq.translation = m_currentTranslation;
        prepReq.clear = m_currentClear;
        prepReq.layout = m_currentLayout;
        for (const auto& t : m_currentPageTargets) {
            prepReq.expectedRevisions[t.regionId] = t.contentRevision;
        }
        prepReq.pageSize = QSize(page->imageSize().width, page->imageSize().height);
        prepReq.projectRoot = m_request.projectRoot;
        prepReq.context = m_context;
        prepReq.replaceManualPolicy = m_request.replacePolicy;

        auto clearHelper = std::make_shared<PrepareClearResultUseCase>(m_request.project, m_derivedAssetStore);
        auto prepUseCase = std::make_shared<PrepareProcessingAdoptionUseCase>(m_request.project, clearHelper.get());

        auto prepFuture = prepUseCase->execute(prepReq);
        auto self = shared_from_this();
        prepFuture.then([this, self, clearHelper, prepUseCase](Result<PreparedProcessingAdoption> prepRes) {
            postStep([this, self, clearHelper, prepUseCase, prepRes]() {
                if (m_context.stopToken.stop_requested()) {
                    finishCancelled();
                    return;
                }
                if (!prepRes.has_value()) {
                    completeCurrentPage(false, prepRes.error().safeMessage);
                    return;
                }

                const auto& prepared = prepRes.value();
                for (const auto& change : prepared.changes) {
                    domain::PageRegionEdit edit = change.after;
                    const domain::PageRegionEdit edits[] = {edit};
                    const auto applied = m_request.project->applyPageRegionEdits(change.pageId, edits);
                    if (!applied.has_value()) {
                        noteStageFailure(QString::fromStdString(applied.error().toString()));
                    }
                }

                if (prepared.clearAssetChange) {
                    const auto& c = *prepared.clearAssetChange;
                    const auto regionList = c.regionIds.isEmpty() ? QVector<domain::RegionId>{c.regionId} : c.regionIds;
                    for (const auto& rId : regionList) {
                        const auto cleared = m_request.project->applyClearChangeOnPage(
                            c.pageId,
                            rId,
                            c.afterRef.isEmpty() ? std::optional<std::string>{} : c.afterRef.toStdString(),
                            c.afterState);
                        if (!cleared.has_value()) {
                            noteStageFailure(QString::fromStdString(cleared.error().toString()));
                        }
                    }
                }

                if (!prepared.skipped.isEmpty()) {
                    noteStageFailure(prepared.skipped.first().error.safeMessage);
                }

                m_projectDirty = true;
                completeCurrentPage(m_currentPageFailure.isEmpty(), m_currentPageFailure);
            });
        });
    }

    BatchPagesProcessingWorkflow* m_workflow{nullptr};
    RecognizeRegionUseCase* m_ocrUseCase{nullptr};
    TranslatePageUseCase* m_translateUseCase{nullptr};
    ClearRegionUseCase* m_clearUseCase{nullptr};
    LayoutRegionUseCase* m_layoutUseCase{nullptr};
    ITextDetector* m_detector{nullptr};
    IPageImageLoader* m_pageImageLoader{nullptr};
    IDerivedAssetStore* m_derivedAssetStore{nullptr};
    IProjectRepository* m_projectRepository{nullptr};
    OperationRegistry* m_registry{nullptr};

    BatchProcessingRequest m_request;
    OperationContext m_context;
    quint64 m_sessionGeneration{0};

    QVector<domain::PageId> m_pageIds;
    int m_currentPageIndex{0};
    qint64 m_totalSteps{1};

    QImage m_currentPageImage;
    QVector<RegionProcessingTarget> m_currentPageTargets;
    QVector<OcrResponse> m_currentOcr;
    std::optional<TranslationResponse> m_currentTranslation;
    QVector<ClearResponse> m_currentClear;
    QVector<LayoutResponse> m_currentLayout;
    QSet<domain::RegionId> m_currentClearFailedRegions;
    QString m_currentPageFailure;
    bool m_projectDirty{false};
    bool m_settled{false};

    std::shared_ptr<QPromise<Result<BatchProcessingReport>>> m_promise;
    BatchProcessingReport m_report;
    BatchPageSummary m_currentPageSummary;
};

BatchPagesProcessingWorkflow::BatchPagesProcessingWorkflow(RecognizeRegionUseCase* ocrUseCase,
                                                           TranslatePageUseCase* translateUseCase,
                                                           ClearRegionUseCase* clearUseCase,
                                                           LayoutRegionUseCase* layoutUseCase,
                                                           ITextDetector* detector,
                                                           IPageImageLoader* pageImageLoader,
                                                           IDerivedAssetStore* derivedAssetStore,
                                                           IProjectRepository* projectRepository,
                                                           OperationRegistry* registry,
                                                           QObject* parent)
    : QObject(parent), m_ocrUseCase(ocrUseCase), m_translateUseCase(translateUseCase), m_clearUseCase(clearUseCase),
      m_layoutUseCase(layoutUseCase), m_detector(detector), m_pageImageLoader(pageImageLoader),
      m_derivedAssetStore(derivedAssetStore), m_projectRepository(projectRepository), m_registry(registry) {}

BatchPagesProcessingWorkflow::~BatchPagesProcessingWorkflow() = default;

QFuture<Result<BatchProcessingReport>> BatchPagesProcessingWorkflow::execute(BatchProcessingRequest request,
                                                                             OperationContext context,
                                                                             quint64 sessionGeneration) {
    auto orchestrator = std::make_shared<BatchOrchestrator>(this,
                                                            m_ocrUseCase,
                                                            m_translateUseCase,
                                                            m_clearUseCase,
                                                            m_layoutUseCase,
                                                            m_detector,
                                                            m_pageImageLoader,
                                                            m_derivedAssetStore,
                                                            m_projectRepository,
                                                            m_registry);

    return orchestrator->start(std::move(request), std::move(context), sessionGeneration);
}

void BatchPagesProcessingWorkflow::cancel(OperationId id) {
    if (m_registry != nullptr) {
        m_registry->cancelOperation(id);
    }
}

void BatchPagesProcessingWorkflow::setTextDetector(ITextDetector* detector) {
    m_detector = detector;
}

} // namespace manga::application
