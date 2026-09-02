#include "application/workflow/SmartTranslatePageWorkflow.h"

#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/workflow/OperationRegistry.h"

#include <QCoreApplication>
#include <QPromise>
#include <QSet>
#include <QSharedPointer>

namespace manga::application {

class WorkflowOrchestrator : public std::enable_shared_from_this<WorkflowOrchestrator> {
public:
    WorkflowOrchestrator(RecognizeRegionUseCase* ocrUseCase,
                         TranslatePageUseCase* translateUseCase,
                         ClearRegionUseCase* clearUseCase,
                         LayoutRegionUseCase* layoutUseCase,
                         OperationRegistry* registry)
        : m_ocrUseCase(ocrUseCase), m_translateUseCase(translateUseCase), m_clearUseCase(clearUseCase),
          m_layoutUseCase(layoutUseCase), m_registry(registry) {
        m_promise = std::make_shared<QPromise<Result<PageProcessingProposal>>>();
    }

    [[nodiscard]] QFuture<Result<PageProcessingProposal>> start(domain::ProjectId projectId,
                                                                domain::PageId pageId,
                                                                QVector<RegionProcessingTarget> targets,
                                                                QString sourceLanguage,
                                                                QString targetLanguage,
                                                                QImage pageImage,
                                                                quint64 sessionGeneration,
                                                                OperationId& outOperationId) {
        m_projectId = projectId;
        m_pageId = pageId;
        m_targets = std::move(targets);
        m_sourceLanguage = std::move(sourceLanguage);
        m_targetLanguage = std::move(targetLanguage);
        m_pageImage = std::move(pageImage);
        m_sessionGeneration = sessionGeneration;
        m_clearFailedRegions.clear();

        OperationDescriptor rootDesc{
            OperationStage::Project, projectId, pageId, std::nullopt, QObject::tr("全页智能翻译"), true};
        auto context = m_registry->createOperation(rootDesc, sessionGeneration, 0);
        outOperationId = context.operationId;
        m_rootContext = std::move(context);

        m_proposal.projectId = m_projectId;
        m_proposal.pageId = m_pageId;
        m_proposal.rootOperationId = outOperationId;

        m_promise->start();
        auto future = m_promise->future();

        if (m_targets.isEmpty()) {
            m_registry->markSkipped(outOperationId, QStringLiteral("skipped.no_regions"));
            m_proposal.issues.append({std::nullopt,
                                      QStringLiteral("Project"),
                                      makeApplicationError(ErrorCategory::Cancelled,
                                                           QStringLiteral("skipped.no_regions"),
                                                           QStringLiteral("No regions"))});
            finish(m_proposal);
            return future;
        }

        const qint64 totalSteps = std::max(1, static_cast<int>(m_targets.size() * 3 + 1));
        if (m_rootContext.progressReporter) {
            m_rootContext.progressReporter->setRange(totalSteps);
            m_rootContext.progressReporter->setValue(0);
            m_rootContext.progressReporter->setStatusText(QObject::tr("正在识别文字 (0/%1)").arg(m_targets.size()));
            m_rootContext.progressReporter->markRunning();
        } else {
            m_registry->markRunning(outOperationId);
        }

        runOcrStep(0);
        return future;
    }

private:
    void finish(const Result<PageProcessingProposal>& result) {
        m_promise->addResult(result);
        m_promise->finish();
        if (m_rootContext.progressReporter) {
            if (result.has_value()) {
                const qint64 totalSteps = std::max(1, static_cast<int>(m_targets.size() * 3 + 1));
                m_rootContext.progressReporter->setValue(totalSteps);
                m_rootContext.progressReporter->setStatusText(QObject::tr("全页翻译完成"));
                m_rootContext.progressReporter->markFinished();
            } else {
                m_rootContext.progressReporter->markFinished(result.error());
            }
        } else {
            if (result.has_value()) {
                m_registry->markFinished(m_rootContext.operationId);
            } else {
                m_registry->markFinished(m_rootContext.operationId, result.error());
            }
        }
    }

    void finishWithProposal() {
        finish(m_proposal);
    }

    void runOcrStep(int index) {
        if (m_rootContext.stopToken.stop_requested()) {
            m_proposal.issues.append({std::nullopt,
                                      QStringLiteral("Project"),
                                      makeApplicationError(ErrorCategory::Cancelled,
                                                           QStringLiteral("cancelled"),
                                                           QStringLiteral("Cancelled"))});
            finishWithProposal();
            return;
        }

        if (index >= m_targets.size()) {
            runTranslationStep();
            return;
        }

        auto& target = m_targets[index];
        if (target.hasManualTranslation) {
            runOcrStep(index + 1);
            return;
        }

        if (m_rootContext.progressReporter) {
            m_rootContext.progressReporter->setValue(index);
            m_rootContext.progressReporter->setStatusText(
                QObject::tr("正在识别文字 (%1/%2)").arg(index + 1).arg(m_targets.size()));
        }

        OcrRequest req;
        req.pageId = m_pageId;
        req.regionId = target.regionId;
        req.image = m_pageImage;
        req.rect = target.geometry;
        req.language = domain::LanguageCode::createUnchecked(m_sourceLanguage.toStdString());

        auto future = m_ocrUseCase->execute(req, m_rootContext);

        auto self = shared_from_this();
        future.then([this, self, index, target](Result<OcrResponse> res) {
            if (m_rootContext.stopToken.stop_requested()) {
                m_proposal.issues.append({target.regionId,
                                          QStringLiteral("Ocr"),
                                          makeApplicationError(ErrorCategory::Cancelled,
                                                               QStringLiteral("cancelled"),
                                                               QStringLiteral("Cancelled"))});
                finishWithProposal();
                return;
            }
            if (res.has_value()) {
                m_proposal.ocr.append(res.value());
                m_targets[index].sourceText = res.value().text;
            } else {
                m_proposal.issues.append({target.regionId, QStringLiteral("Ocr"), res.error()});
            }
            QMetaObject::invokeMethod(qApp, [this, self, index]() { runOcrStep(index + 1); }, Qt::QueuedConnection);
        });
    }

    void runTranslationStep() {
        if (m_rootContext.stopToken.stop_requested()) {
            finishWithProposal();
            return;
        }

        TranslationRequest req;
        req.sourceLanguage = domain::LanguageCode::createUnchecked(m_sourceLanguage.toStdString());
        req.targetLanguage = domain::LanguageCode::createUnchecked(m_targetLanguage.toStdString());
        req.readingDirection = domain::ReadingDirection::RightToLeft;

        for (const auto& t : m_targets) {
            if (t.hasManualTranslation) {
                continue;
            }
            if (!t.sourceText.isEmpty()) {
                req.regions.append({t.regionId, t.sourceText});
            }
        }

        if (req.regions.isEmpty()) {
            QMetaObject::invokeMethod(
                qApp, [this, self = shared_from_this()]() { runClearStep(0); }, Qt::QueuedConnection);
            return;
        }

        if (m_rootContext.progressReporter) {
            m_rootContext.progressReporter->setValue(m_targets.size());
            m_rootContext.progressReporter->setStatusText(QObject::tr("正在调用 AI 翻译..."));
        }

        auto future = m_translateUseCase->execute(req, m_rootContext);

        auto self = shared_from_this();
        future.then([this, self](Result<TranslationResponse> res) {
            if (res.has_value()) {
                m_proposal.translation = res.value();
                for (const auto& tr : res.value().items) {
                    for (auto& t : m_targets) {
                        if (t.regionId == tr.regionId) {
                            t.effectiveTranslation = tr.translation;
                        }
                    }
                }
            } else {
                m_proposal.issues.append({std::nullopt, QStringLiteral("Translate"), res.error()});
                finishWithProposal();
                return;
            }
            QMetaObject::invokeMethod(qApp, [this, self]() { runClearStep(0); }, Qt::QueuedConnection);
        });
    }

    void runClearStep(int index) {
        if (m_rootContext.stopToken.stop_requested()) {
            finishWithProposal();
            return;
        }

        if (index >= m_targets.size()) {
            runLayoutStep(0);
            return;
        }

        auto& target = m_targets[index];
        if (target.effectiveTranslation.isEmpty()) {
            QMetaObject::invokeMethod(
                qApp, [this, self = shared_from_this(), index]() { runClearStep(index + 1); }, Qt::QueuedConnection);
            return;
        }

        if (m_rootContext.progressReporter) {
            m_rootContext.progressReporter->setValue(m_targets.size() + 1 + index);
            m_rootContext.progressReporter->setStatusText(
                QObject::tr("正在清除背景 (%1/%2)").arg(index + 1).arg(m_targets.size()));
        }

        ClearRequest req;
        req.projectId = m_projectId;
        req.pageId = m_pageId;
        req.regionId = target.regionId;
        req.pageImage = m_pageImage;
        req.region = target.geometry;
        req.polygon = target.bubblePolygon;
        req.settings.enabled = true;

        auto future = m_clearUseCase->execute(req, m_rootContext);

        auto self = shared_from_this();
        future.then([this, self, index, target](Result<ClearResponse> res) {
            if (res.has_value()) {
                m_proposal.clear.append(res.value());
            } else {
                m_proposal.issues.append({target.regionId, QStringLiteral("Clear"), res.error()});
                m_clearFailedRegions.insert(target.regionId);
            }
            QMetaObject::invokeMethod(qApp, [this, self, index]() { runClearStep(index + 1); }, Qt::QueuedConnection);
        });
    }

    void runLayoutStep(int index) {
        if (m_rootContext.stopToken.stop_requested()) {
            finishWithProposal();
            return;
        }

        if (index >= m_targets.size()) {
            finishWithProposal();
            return;
        }

        auto& target = m_targets[index];
        if (target.effectiveTranslation.isEmpty()) {
            QMetaObject::invokeMethod(
                qApp, [this, self = shared_from_this(), index]() { runLayoutStep(index + 1); }, Qt::QueuedConnection);
            return;
        }

        if (m_clearFailedRegions.contains(target.regionId)) {
            m_proposal.issues.append({target.regionId,
                                      QStringLiteral("Layout"),
                                      makeApplicationError(ErrorCategory::Cancelled,
                                                           QStringLiteral("skipped.clear_failed"),
                                                           QStringLiteral("Layout skipped because clear failed"))});
            QMetaObject::invokeMethod(
                qApp, [this, self = shared_from_this(), index]() { runLayoutStep(index + 1); }, Qt::QueuedConnection);
            return;
        }

        if (m_rootContext.progressReporter) {
            m_rootContext.progressReporter->setValue(m_targets.size() * 2 + 1 + index);
            m_rootContext.progressReporter->setStatusText(
                QObject::tr("正在排版文字 (%1/%2)").arg(index + 1).arg(m_targets.size()));
        }

        LayoutRequest req;
        req.pageId = m_pageId;
        req.regionId = target.regionId;
        req.text = target.effectiveTranslation;
        req.bounds = target.geometry;
        req.constraints = domain::TextLayout{};
        req.logicalDpi = 72.0;
        req.contentRevision = target.contentRevision;

        auto future = m_layoutUseCase->execute(req, m_rootContext);

        auto self = shared_from_this();
        future.then([this, self, index, target](Result<LayoutResponse> res) {
            if (res.has_value()) {
                m_proposal.layout.append(res.value());
            } else {
                m_proposal.issues.append({target.regionId, QStringLiteral("Layout"), res.error()});
            }
            QMetaObject::invokeMethod(qApp, [this, self, index]() { runLayoutStep(index + 1); }, Qt::QueuedConnection);
        });
    }

    domain::ProjectId m_projectId;
    domain::PageId m_pageId;
    QVector<RegionProcessingTarget> m_targets;
    QString m_sourceLanguage;
    QString m_targetLanguage;
    QImage m_pageImage;
    quint64 m_sessionGeneration;
    OperationContext m_rootContext;

    RecognizeRegionUseCase* m_ocrUseCase;
    TranslatePageUseCase* m_translateUseCase;
    ClearRegionUseCase* m_clearUseCase;
    LayoutRegionUseCase* m_layoutUseCase;
    OperationRegistry* m_registry;

    std::shared_ptr<QPromise<Result<PageProcessingProposal>>> m_promise;
    PageProcessingProposal m_proposal;
    QSet<domain::RegionId> m_clearFailedRegions;
};

SmartTranslatePageWorkflow::SmartTranslatePageWorkflow(RecognizeRegionUseCase* ocrUseCase,
                                                       TranslatePageUseCase* translateUseCase,
                                                       ClearRegionUseCase* clearUseCase,
                                                       LayoutRegionUseCase* layoutUseCase,
                                                       OperationRegistry* registry,
                                                       QObject* parent)
    : QObject(parent), m_ocrUseCase(ocrUseCase), m_translateUseCase(translateUseCase), m_clearUseCase(clearUseCase),
      m_layoutUseCase(layoutUseCase), m_registry(registry) {}

SmartTranslatePageWorkflow::~SmartTranslatePageWorkflow() = default;

QFuture<Result<PageProcessingProposal>> SmartTranslatePageWorkflow::execute(domain::ProjectId projectId,
                                                                            domain::PageId pageId,
                                                                            QVector<RegionProcessingTarget> targets,
                                                                            QString sourceLanguage,
                                                                            QString targetLanguage,
                                                                            QImage pageImage,
                                                                            quint64 sessionGeneration,
                                                                            OperationId& outOperationId) {
    auto orchestrator = std::make_shared<WorkflowOrchestrator>(
        m_ocrUseCase, m_translateUseCase, m_clearUseCase, m_layoutUseCase, m_registry);

    return orchestrator->start(projectId,
                               pageId,
                               std::move(targets),
                               std::move(sourceLanguage),
                               std::move(targetLanguage),
                               std::move(pageImage),
                               sessionGeneration,
                               outOperationId);
}

void SmartTranslatePageWorkflow::cancel(OperationId id) {
    if (m_registry) {
        m_registry->cancelOperation(id);
    }
}

} // namespace manga::application
