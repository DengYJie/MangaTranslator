#include "application/workflow/AppendPagesWorkflow.h"

#include "application/dto/ProjectDto.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/IProjectRepository.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"

#include <QCoreApplication>
#include <QPromise>

#include <memory>
#include <utility>

namespace manga::application {

namespace {

class AppendPagesOrchestrator : public std::enable_shared_from_this<AppendPagesOrchestrator> {
public:
    AppendPagesOrchestrator(IComicImporter* importer, IProjectRepository* projectRepository)
        : m_importer(importer), m_projectRepository(projectRepository) {
        m_promise = std::make_shared<QPromise<Result<AppendPagesReport>>>();
    }

    [[nodiscard]] QFuture<Result<AppendPagesReport>> start(AppendPagesRequest request, OperationContext context) {
        m_request = std::move(request);
        m_context = std::move(context);

        m_promise->start();
        auto future = m_promise->future();

        if (m_importer == nullptr) {
            finish(std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                        QStringLiteral("internal.null_importer"),
                                                        QStringLiteral("Image importer is unavailable"))));
            return future;
        }
        if (m_context.stopToken.stop_requested()) {
            finish(cancelledError());
            return future;
        }
        if (!m_request.projectId.isValid() || m_request.projectRoot.trimmed().isEmpty() ||
            m_request.project == nullptr || m_request.filePaths.isEmpty()) {
            finish(std::unexpected(makeApplicationError(ErrorCategory::Input,
                                                        QStringLiteral("input.invalid_append_request"),
                                                        QStringLiteral("Append pages request is invalid"))));
            return future;
        }

        publishStatus(QObject::tr("导入页面"), true);

        ImportImagesRequest importReq;
        importReq.projectId = m_request.projectId;
        importReq.projectRoot = m_request.projectRoot;
        importReq.filePaths = m_request.filePaths;

        ImportImagesUseCase useCase(m_importer);
        auto importFuture = useCase.execute(importReq, m_context);
        auto self = shared_from_this();
        importFuture.then([this, self](Result<ImportImagesResult> result) {
            postStep([this, self, result = std::move(result)]() mutable { handleImportFinished(std::move(result)); });
        });
        return future;
    }

private:
    /// Ports complete their promise on a worker thread, so a QFuture::then body inherits that
    /// affinity. Every mutation of orchestrator or project state is therefore marshalled here first.
    template <typename F> void postStep(F&& func) {
        if (qApp != nullptr) {
            QMetaObject::invokeMethod(qApp, std::forward<F>(func), Qt::QueuedConnection);
        } else {
            func();
        }
    }

    void publishStatus(const QString& statusText, bool running = false) {
        if (m_context.progressReporter) {
            m_context.progressReporter->setStatusText(statusText);
            if (running) {
                m_context.progressReporter->markRunning();
            }
        }
    }

    void handleImportFinished(Result<ImportImagesResult> result) {
        if (!result.has_value()) {
            finish(std::unexpected(result.error()));
            return;
        }
        if (m_context.stopToken.stop_requested()) {
            discardAndFinish(*result, cancelledError().error());
            return;
        }
        if (result->importedPages.isEmpty()) {
            ApplicationError error = !result->failures.isEmpty()
                                         ? result->failures.first().error
                                         : makeApplicationError(ErrorCategory::Input,
                                                                QStringLiteral("input.empty_import"),
                                                                QStringLiteral("No pages were imported"));
            discardAndFinish(*result, error);
            return;
        }

        m_report.failedSources = static_cast<int>(result->failures.size());
        m_batchId = result->batchId;

        ApplyImportResultRequest applyReq;
        applyReq.projectId = m_request.projectId;
        applyReq.sessionGeneration = m_request.sessionGeneration;
        applyReq.currentSessionGeneration =
            m_request.currentSessionGeneration ? m_request.currentSessionGeneration() : m_request.sessionGeneration;
        applyReq.result = std::move(*result);

        ApplyImportResultUseCase applyUseCase(m_request.project);
        auto applied = applyUseCase.execute(applyReq);
        if (!applied.has_value()) {
            discardAndFinish(applyReq.result, applied.error());
            return;
        }
        m_report.pageIds = *applied;
        acknowledgeBatch();
    }

    /// Removes the staged asset batch when the pages never entered the aggregate,
    /// then settles the promise with the given error.
    void discardAndFinish(const ImportImagesResult& result, const ApplicationError& error) {
        QVector<QString> assetPaths;
        assetPaths.reserve(result.importedPages.size());
        for (const auto& imported : result.importedPages) {
            assetPaths.append(QString::fromStdString(imported.page.originalAssetPath()));
        }
        if (!result.batchId.isValid() || assetPaths.isEmpty()) {
            finish(std::unexpected(error));
            return;
        }

        DiscardImportedAssetsRequest discardReq;
        discardReq.projectId = m_request.projectId;
        discardReq.projectRoot = m_request.projectRoot;
        discardReq.batchId = result.batchId;
        discardReq.assetPaths = assetPaths;

        DiscardImportedAssetsUseCase useCase(m_importer);
        auto self = shared_from_this();
        useCase.execute(std::move(discardReq), m_context).then([this, self, error](Result<void>) {
            postStep([this, self, error]() { finish(std::unexpected(error)); });
        });
    }

    void acknowledgeBatch() {
        AcknowledgeImportedAssetsRequest ackReq;
        ackReq.projectId = m_request.projectId;
        ackReq.projectRoot = m_request.projectRoot;
        ackReq.batchId = m_batchId;

        AcknowledgeImportedAssetsUseCase useCase(m_importer);
        auto self = shared_from_this();
        useCase.execute(ackReq, m_context).then([this, self](Result<void> result) {
            postStep([this, self, result]() { handleAckFinished(result); });
        });
    }

    void handleAckFinished(const Result<void>& result) {
        if (!result.has_value()) {
            finish(std::unexpected(result.error()));
            return;
        }
        saveProject();
    }

    void saveProject() {
        if (m_projectRepository == nullptr) {
            finish(m_report);
            return;
        }

        publishStatus(QObject::tr("保存项目"));

        SaveProjectRequest saveReq;
        saveReq.project = *m_request.project;
        auto saveFuture = m_projectRepository->saveProject(saveReq, m_context);
        auto self = shared_from_this();
        saveFuture.then([this, self](Result<SaveProjectResult> result) {
            postStep([this, self, result]() {
                if (!result.has_value()) {
                    finish(std::unexpected(result.error()));
                    return;
                }
                m_report.projectSaved = true;
                m_report.savedRevision = result->savedRevision;
                finish(m_report);
            });
        });
    }

    void finish(const Result<AppendPagesReport>& result) {
        if (m_settled) {
            return;
        }
        m_settled = true;

        m_promise->addResult(result);
        m_promise->finish();

        if (m_context.progressReporter) {
            if (result.has_value()) {
                m_context.progressReporter->setStatusText(QObject::tr("导入完成"));
                m_context.progressReporter->markFinished();
            } else {
                m_context.progressReporter->markFinished(result.error());
            }
        }
    }

    static Result<AppendPagesReport> cancelledError() {
        return std::unexpected(makeApplicationError(
            ErrorCategory::Cancelled, QStringLiteral("cancelled.user_requested"), QObject::tr("导入已被取消")));
    }

    IComicImporter* m_importer{nullptr};
    IProjectRepository* m_projectRepository{nullptr};

    AppendPagesRequest m_request;
    OperationContext m_context;
    OperationId m_batchId;

    std::shared_ptr<QPromise<Result<AppendPagesReport>>> m_promise;
    AppendPagesReport m_report;
    bool m_settled{false};
};

} // namespace

AppendPagesWorkflow::AppendPagesWorkflow(IComicImporter* importer,
                                         IProjectRepository* projectRepository,
                                         QObject* parent)
    : QObject(parent), m_importer(importer), m_projectRepository(projectRepository) {}

QFuture<Result<AppendPagesReport>> AppendPagesWorkflow::execute(AppendPagesRequest request, OperationContext context) {
    auto orchestrator = std::make_shared<AppendPagesOrchestrator>(m_importer, m_projectRepository);
    return orchestrator->start(std::move(request), std::move(context));
}

} // namespace manga::application
