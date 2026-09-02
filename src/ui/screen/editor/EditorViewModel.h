#pragma once

#include "application/dto/ExportDto.h"
#include "application/dto/ImportDto.h"
#include "application/dto/ManualPaintDto.h"
#include "application/dto/ProjectDto.h"
#include "application/dto/TextDetectionDto.h"
#include "application/operation/AsyncResult.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/usecase/processing/ManualInpaintWorkflow.h"
#include "application/workflow/AppendPagesWorkflow.h"
#include "ui/base/BaseViewModel.h"
#include "ui/command/EditorCommandContext.h"
#include "ui/command/PageClearLayerCommand.h"
#include "ui/command/PageManualOverrideCommand.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/error/RecoveryPresenter.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/ExportSnapshotBuilder.h"
#include "ui/screen/editor/TextEditSession.h"

#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QUndoStack>
#include <QVariant>

#include <memory>
#include <optional>

namespace manga::application {
class IComicExporter;
class IDerivedAssetStore;
class IPageImageLoader;
class IThumbnailStore;
class IProjectRepository;
class LoadOrGenerateThumbnailUseCase;
class LoadPageImageUseCase;
class OperationRegistry;
} // namespace manga::application

namespace manga::ui::screen::editor {

class EditorProcessingController;

struct ProjectCloseSummary {
    QString projectName;
    bool isDirty{false};
    QString safeMessage;
};

/// Drives the editor skeleton: project session, current page image loading and the
/// thumbnail pipeline (max 4 concurrent, session-generation-checked late results).
class EditorViewModel : public base::BaseViewModel<EditorViewModel, EditorState> {
    Q_OBJECT
public:
    static constexpr int kMaxConcurrentThumbnails = 4;
    /// Edge length of the square bounding box requested for a thumbnail. It must
    /// cover the list cell size at the highest supported device pixel ratio.
    static constexpr int kThumbnailEdgePx = 128;

    EditorViewModel(application::IThumbnailStore* thumbnailStore,
                    application::IPageImageLoader* pageImageLoader,
                    application::OperationRegistry* operationRegistry,
                    application::IProjectRepository* projectRepository = nullptr,
                    QObject* parent = nullptr);
    ~EditorViewModel() override;

    void openProjectSession(application::OpenedProjectSession session);
    void closeProjectSession();
    void selectPage(const domain::PageId& pageId);
    void selectAdjacentPage(int offset);
    void retryPageLoad();
    void requestThumbnail(const domain::PageId& pageId, bool force);
    /// Replaces the set of pages currently visible in the thumbnail list. Queued
    /// pages inside the set are loaded before pages scrolled out of view. An empty
    /// set falls back to ordering by distance from the current page.
    void setVisiblePages(const QVector<domain::PageId>& pageIds);
    void setOcrAvailability(bool available, const QString& unavailableReason) {
        updateState([&](EditorState& s) {
            s.isOcrAvailable = available;
            s.ocrUnavailableReason = available ? QString() : unavailableReason;
        });
    }
    void setDetectionAvailability(bool available, const QString& unavailableReason = QString()) {
        updateState([&](EditorState& s) {
            s.isDetectionAvailable = available;
            s.detectionUnavailableReason = available ? QString() : unavailableReason;
        });
    }
    void setTranslationAvailability(bool available, const QString& unavailableReason) {
        updateState([&](EditorState& s) {
            s.isTranslationAvailable = available;
            s.translationUnavailableReason = available ? QString() : unavailableReason;
        });
    }
    void setInpaintAvailability(bool available, const QString& unavailableReason) {
        updateState([&](EditorState& s) {
            s.isInpaintAvailable = available;
            s.inpaintUnavailableReason = available ? QString() : unavailableReason;
        });
    }
    /// Records the live canvas transform for the current page. Memory-only, no State churn.
    void noteViewTransform(double zoom, const QPointF& centerOnPage);
    void dismissNotice();

    /// Sets the active canvas tool. Uncommitted canvas gestures are cancelled here
    /// once gesture support exists.
    void setActiveTool(CanvasTool tool);
    /// Selects a region on the current page using the given selection mode.
    void selectRegion(const domain::RegionId& regionId, RegionSelectionMode mode);
    /// Clears all region selection.
    void clearRegionSelection();
    /// Selects all regions on the current page.
    void selectAllRegionsOnCurrentPage();
    /// Sets the preview mode for comparing original and translated content.
    void setPreviewMode(PreviewMode mode);
    /// Requests a composed preview for the current page. Delivery is generation and revision guarded.
    void requestPreview(const domain::PageId& pageId);

    /// Triggers automatic layout and text detection on the current page.
    void detectRegionsOnCurrentPage();
    /// Pushes a batch of detected text regions into undo history and selects them.
    void applyDetectedRegions(const domain::PageId& pageId,
                              const QVector<application::DetectedRegionItem>& detected,
                              bool clearExisting = false);

    /// Creates point text anchored at a page coordinate.
    void createPointText(QPointF anchor);
    /// Creates paragraph text using an in-bounds normalized frame.
    void createTextBox(const domain::RectGeometry& geometry);
    /// Creates a region with custom geometry and optional layout constraints.
    void createRegion(const domain::RectGeometry& geometry, domain::TextLayout layout = {});
    /// Deletes the given regions from the current page as one undo step.
    void deleteRegions(const QVector<domain::RegionId>& regionIds);
    /// Commits a geometry change for one region. No-op if delta is zero or geometry invalid.
    void commitRegionGeometry(const domain::RegionId& regionId,
                              const domain::RectGeometry& newGeometry,
                              canvas::EditKind kind);
    void commitRegionGeometries(const QVector<domain::RegionId>& regionIds,
                                const QVector<domain::RectGeometry>& newGeometries,
                                canvas::EditKind kind);
    /// Takes ownership of `cmd` and pushes it onto the project undo stack, sealing any active text
    /// session first. Deletes the command when no session is open.
    void pushPreparedCommand(QUndoCommand* cmd);
    /// Cancels the background tasks owned by the open project session and invalidates thumbnail
    /// state for the current view generation.
    void cancelProjectTasks();
    /// Adopts a project save performed outside the editor, such as the end of a batch run.
    void markProjectCleanAtRevision(quint64 savedRevision);
    /// Undoes the last command. No-op when faulted or stack empty.
    void undo();
    /// Redoes the last undone command. No-op when faulted or stack empty.
    void redo();

    /// Consumes one Inspector field edit. Source/translation text edits route through
    /// the merge-aware text session; style edits become one macro command over all
    /// target regions. Any invalid target vetoes the whole batch.
    void commitInspectorFieldEdit(const domain::PageId& pageId,
                                  const QVector<domain::RegionId>& regionIds,
                                  InspectorField field,
                                  const QVariant& value);
    /// Adopts a translation candidate as its own undoable command. Never merges with
    /// manual translation edits.
    void adoptCandidate(const domain::PageId& pageId, const domain::RegionId& regionId, const QString& candidateId);

    application::Result<void> adoptProcessingProposal(const application::PreparedProcessingAdoption& prepared);
    void rejectProcessingProposal(application::OperationId operationId);

    /// Seals the active text merge batch (FocusOut from an Inspector text control).
    void sealTextEditSession();
    /// Seals the active text merge batch before a project save to preserve undo boundaries.
    void prepareForSave();

    void saveProject();
    void requestCloseProject();
    void confirmDiscardAndClose();
    void cancelClose();

    /// Injects the export pipeline ports. Non-owning; export intents fail with
    /// internal.null_dependency until both are set.
    void setExportDependencies(application::IComicExporter* exporter, application::IDerivedAssetStore* derivedStore);
    /// Replaces the batch page selection used by ExportScope::SelectedPages.
    /// IDs are deduplicated and filtered to existing pages; selection order is kept.
    void setSelectedPages(const QVector<domain::PageId>& pageIds);
    /// Exports pages for the given scope into outputDir. The request is assembled on
    /// the GUI thread from an aggregate snapshot; image I/O and writing run on workers.
    /// Duplicate intents while an export is running are ignored.
    void exportPages(application::ExportScope scope, const QString& outputDir);
    void retryExportInDirectory(const QString& outputDir);

    /// Injects the append-pages pipeline. Non-owning; import intents are ignored
    /// until it is set.
    void setAppendPagesWorkflow(application::AppendPagesWorkflow* workflow);
    /// Imports image files or folders as new pages of the open project, then saves
    /// the project metadata. Duplicate intents while an import is running are ignored.
    void importPagesIntoProject(const QVector<QString>& filePaths);

    /// Test-only hook: removes a region via RemoveRegionUseCase and rebuilds projection.
    /// Production intents must not use this method.
    [[nodiscard]] bool removeRegionForTest(const domain::RegionId& regionId);
    /// Test-only hook: number of commands on the undo stack.
    [[nodiscard]] int undoStackCountForTest() const noexcept {
        return m_undoStack ? m_undoStack->count() : 0;
    }
    /// Test-only hook: replaces the text session clock with a controllable source.
    void setTextEditClockForTest(TextEditSession::Clock clock) {
        m_textEditSession = std::make_unique<TextEditSession>(std::move(clock));
    }
    /// Test-only hook: queries undo stack clean state.
    [[nodiscard]] bool undoStackIsCleanForTest() const noexcept {
        return m_undoStack ? m_undoStack->isClean() : true;
    }

    /// Returns the project revision for change tracking; 0 means no active session.
    [[nodiscard]] quint64 currentProjectRevision() const noexcept {
        return m_openedSession ? m_openedSession->project.revision() : 0;
    }

    void setProcessingController(EditorProcessingController* controller);
    [[nodiscard]] EditorProcessingController* processingController() const noexcept {
        return m_processingController;
    }
    void updateProcessingState(const EditorProcessingState& processingState);
    void setNotice(const std::optional<EditorNotice>& notice) {
        updateState([&](EditorState& s) { s.notice = notice; });
    }

    [[nodiscard]] domain::ComicProject* projectAggregate() noexcept {
        return m_openedSession ? &m_openedSession->project : nullptr;
    }
    [[nodiscard]] const domain::ComicProject* projectAggregate() const noexcept {
        return m_openedSession ? &m_openedSession->project : nullptr;
    }
    [[nodiscard]] QString projectRootPath() const noexcept {
        return m_openedSession ? m_openedSession->projectRootPath : QString();
    }
    [[nodiscard]] bool hasSession() const noexcept {
        return m_hasSession;
    }
    [[nodiscard]] quint64 projectSessionGeneration() const noexcept {
        return m_state.projectSessionGeneration;
    }
    [[nodiscard]] quint64 viewGeneration() const noexcept {
        return m_state.viewGeneration;
    }
    [[nodiscard]] const QImage& currentPageImage() const noexcept {
        return m_currentPageImage;
    }

    /// Pushes one page-level clear-layer change onto the session undo stack.
    bool applyPageInpaintChange(const application::PreparedPageInpaintChange& change);
    /// Pushes one page-level manual override (paint/erase) change onto the session undo stack.
    bool applyPageManualOverrideChange(const application::PreparedPageManualOverrideChange& change);
    void setBrushColor(const QColor& color);
    void setBrushDiameter(double diameter);

Q_SIGNALS:
    void stateChanged(const EditorState& state);
    void pageImageReady(const PageImageDelivery& delivery);
    void cleaningImageReady(const PageImageDelivery& delivery);
    void previewImageReady(const PageImageDelivery& delivery);
    void thumbnailReady(const domain::PageId& pageId, const QImage& image);
    void thumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// The project session changed or thumbnail work was explicitly cancelled; loading rows must re-request.
    void thumbnailsInvalidated();

    void closeDecisionRequested(const ProjectCloseSummary& summary);
    void saveCompleted(const QString& revisionId);
    void saveFailed(const application::ApplicationError& error);
    void projectClosed();
    void textRegionCreated(const domain::RegionId& regionId);

    void exportCompleted(const application::ExportResponse& response);
    void exportFailed(const application::ApplicationError& error);

protected:
    void emitStateChanged() override {
        Q_EMIT stateChanged(m_state);
    }

private:
    struct ViewTransform {
        double zoom{1.0};
        QPointF centerOnPage;
    };
    struct ThumbnailTicket {
        application::OperationId operationId;
        quint64 projectSessionGeneration{0};
    };

    void startPageLoad(const domain::PageId& pageId);
    void handlePageLoadFinished();
    void handlePreviewFinished();
    void invalidatePreviewAndRequestCurrentPage();
    void startThumbnail(const domain::PageId& pageId);
    void handleThumbnailFinished(const domain::PageId& pageId,
                                 QFutureWatcher<application::Result<QImage>>* watcher,
                                 const application::OperationContext& context);
    void pumpThumbnailQueue();
    void cancelThumbnailRequests();
    void handleSaveFinished();
    void handleProjectCloseFinished();
    void completeProjectClose();
    void cancelExport();
    void handleExportSnapshotFinished();
    void handleExportFinished();
    void failExport(const application::ApplicationError& error);
    void handleAppendPagesFinished();
    [[nodiscard]] const application::PageDescriptor* findPage(const domain::PageId& pageId) const;
    /// Index of pageId inside m_session.pages, or -1 when the page is unknown.
    [[nodiscard]] int pageOrder(const domain::PageId& pageId) const;
    /// Removes one entry from the thumbnail queue, preferring visible pages and
    /// otherwise the entry closest to the current page. Returns a default id when
    /// the queue is empty.
    [[nodiscard]] domain::PageId takeNextQueuedPageId();

    /// Rebuilds current page region projections from m_openedSession->project only.
    /// Called after openProjectSession, selectPage, and synchronous mutations.
    void rebuildCurrentPageProjection();

    /// Recomputes s.inspector from the current selection and the session aggregate.
    /// Caller must invoke inside an updateState lambda after selection/page fields are set.
    void updateInspectorProjection(EditorState& s) const;

    /// Seals the active text merge batch before an operation that must not merge:
    /// selection change, page switch, explicit apply, Undo/Redo, or session close.
    void sealActiveTextSession();

    /// Pushes a property command and refreshes projections and undo state. After a
    /// successful merge the pushed command is deleted by the stack, so fault state is
    /// read from the surviving stack top.
    void pushPropertyCommand(command::RegionPropertyCommand* cmd);
    void commitTextEdit(const domain::PageId& pageId,
                        const domain::RegionId& regionId,
                        InspectorField field,
                        const QString& text);
    void commitStyleEdit(const domain::PageId& pageId,
                         const QVector<domain::RegionId>& regionIds,
                         InspectorField field,
                         const QVariant& value);
    void createTextRegion(const domain::RectGeometry& geometry, domain::TextLayout layout);

    /// Refreshes canUndo/canRedo/undoText/redoText and undoHistoryFaulted into State from the stack.
    void refreshUndoState();

    application::IThumbnailStore* m_thumbnailStore;       // non-owning, may be null in tests
    application::IPageImageLoader* m_pageImageLoader;     // non-owning, may be null in tests
    application::OperationRegistry* m_operationRegistry;  // non-owning
    application::IProjectRepository* m_projectRepository; // non-owning
    // Member lifetime is required: LoadOrGenerateThumbnailUseCase continuations capture `this`.
    std::unique_ptr<application::LoadOrGenerateThumbnailUseCase> m_thumbnailUseCase;
    std::unique_ptr<application::LoadPageImageUseCase> m_pageLoadUseCase;

    /// View-facing snapshot derived from m_openedSession; must be re-derived after aggregate mutations that change page
    /// structure.
    application::ProjectSessionDescriptor m_session;
    std::optional<application::OpenedProjectSession> m_openedSession;
    bool m_hasSession{false};
    QHash<QString, ViewTransform> m_viewTransforms; // key: PageId string

    /// Undo stack and command context. Created in openProjectSession after aggregate stored,
    /// and destroyed only after repository close succeeds.
    std::unique_ptr<QUndoStack> m_undoStack;
    std::unique_ptr<command::EditorCommandContext> m_commandContext;
    bool m_undoHistoryFaulted{false};

    /// Merge-aware text edit session; lives for the ViewModel lifetime and is reset
    /// on session open/close.
    std::unique_ptr<TextEditSession> m_textEditSession;

    QFutureWatcher<application::Result<QImage>> m_pageLoadWatcher;
    QImage m_currentPageImage;
    quint64 m_pageLoadToken{0};
    application::OperationId m_pageLoadOperation{};

    QFutureWatcher<application::Result<ExportSnapshotBuilder::PreviewResult>> m_previewWatcher;
    quint64 m_previewToken{0};
    quint64 m_previewContentRevision{0};
    quint64 m_previewSessionGeneration{0};
    domain::PageId m_previewPageId;
    application::OperationId m_previewOperation{};
    QImage m_cachedPreviewImage;
    QImage m_cachedCleaningImage;

    QHash<QString, ThumbnailTicket> m_thumbnailInFlight; // key: PageId string
    QQueue<domain::PageId> m_thumbnailQueue;
    QSet<QString> m_thumbnailQueueSet; // key: PageId string, mirrors m_thumbnailQueue for O(1) lookup
    QSet<QString> m_visiblePageIds;    // key: PageId string, pages visible in the thumbnail list
    QHash<QString, QFutureWatcher<application::Result<QImage>>*> m_thumbnailWatchers;

    QFutureWatcher<application::Result<application::SaveProjectResult>> m_saveWatcher;
    quint64 m_saveToken{0};
    application::OperationId m_saveOperation{};

    QFutureWatcher<application::Result<void>> m_closeWatcher;
    bool m_closeInProgress{false};
    std::optional<application::OpenedProjectSession> m_pendingSession;

    /// Export pipeline ports, injected via setExportDependencies. Non-owning.
    application::IComicExporter* m_exporter{nullptr};
    application::IDerivedAssetStore* m_derivedAssetStore{nullptr};
    QFutureWatcher<application::Result<QVector<application::ExportPageSnapshot>>> m_snapshotWatcher;
    QFutureWatcher<application::Result<application::ExportResponse>> m_exportWatcher;
    application::OperationContext m_exportContext;
    /// Export completions are validated against a dedicated sequence: page switches
    /// bump the shared request counter and must not strand a running export.
    quint64 m_exportToken{0};
    quint64 m_exportSequence{0};
    application::OperationId m_exportOperation{};
    application::ExportScope m_exportScope{application::ExportScope::AllPages};
    QString m_exportOutputDir;
    /// Page identifiers captured when the export intent was accepted; later page
    /// switches or selection changes must not alter the running export.
    domain::PageId m_exportCurrentPageId;
    QVector<domain::PageId> m_exportSelectedPageIds;
    /// Aggregate copy owned for the duration of one export so the background
    /// snapshot chain never reads a released session aggregate.
    std::unique_ptr<domain::ComicProject> m_exportProjectSnapshot;

    /// Append-pages pipeline, injected via setAppendPagesWorkflow. Non-owning.
    application::AppendPagesWorkflow* m_appendPagesWorkflow{nullptr};
    QFutureWatcher<application::Result<application::AppendPagesReport>> m_appendPagesWatcher;
    application::OperationId m_appendPagesOperation{};
    quint64 m_appendPagesSessionGeneration{0};

    ui::error::RecoveryPresenter m_recoveryPresenter;
    EditorProcessingController* m_processingController{nullptr};
};

} // namespace manga::ui::screen::editor
