#pragma once

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "ui/base/BaseViewModel.h"
#include "ui/command/EditorCommandContext.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/TextEditSession.h"

#include <QFutureWatcher>
#include <QHash>
#include <QUndoStack>
#include <QVariant>

#include <memory>
#include <optional>

namespace manga::application {
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
/// thumbnail pipeline (max 4 concurrent, generation-checked late results).
class EditorViewModel : public base::BaseViewModel<EditorViewModel, EditorState> {
    Q_OBJECT
public:
    static constexpr int kMaxConcurrentThumbnails = 4;

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

    /// Creates a region on the current page. Validates geometry (>= 8x8 inside page bounds)
    /// and generates RegionId before pushing the command.
    void createRegion(const domain::RectGeometry& geometry);
    /// Deletes the given regions from the current page as one undo step.
    void deleteRegions(const QVector<domain::RegionId>& regionIds);
    /// Commits a geometry change for one region. No-op if delta is zero or geometry invalid.
    void commitRegionGeometry(const domain::RegionId& regionId,
                              const domain::RectGeometry& newGeometry,
                              canvas::EditKind kind);
    /// P5-D hook: pushes an external command to the project undo stack.
    void pushPreparedCommand(QUndoCommand* cmd);
    /// P5-D hook: cancels all ongoing background project tasks.
    void cancelProjectTasks();
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

Q_SIGNALS:
    void stateChanged(const EditorState& state);
    void pageImageReady(const PageImageDelivery& delivery);
    void thumbnailReady(const domain::PageId& pageId, const QImage& image);
    void thumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// viewGeneration bumped: in-flight thumbnail binds are discarded, rows must re-request.
    void thumbnailsInvalidated();

    void closeDecisionRequested(const ProjectCloseSummary& summary);
    void saveCompleted(const QString& revisionId);
    void saveFailed(const application::ApplicationError& error);
    void projectClosed();

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
        quint64 viewGeneration{0};
    };

    void startPageLoad(const domain::PageId& pageId);
    void handlePageLoadFinished();
    void startThumbnail(const domain::PageId& pageId);
    void handleThumbnailFinished(const domain::PageId& pageId);
    void pumpThumbnailQueue();
    void cancelThumbnailRequests();
    void handleSaveFinished();
    [[nodiscard]] const application::PageDescriptor* findPage(const domain::PageId& pageId) const;

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
    /// destroyed in closeProjectSession before releasing aggregate.
    std::unique_ptr<QUndoStack> m_undoStack;
    std::unique_ptr<command::EditorCommandContext> m_commandContext;
    bool m_undoHistoryFaulted{false};

    /// Merge-aware text edit session; lives for the ViewModel lifetime and is reset
    /// on session open/close.
    std::unique_ptr<TextEditSession> m_textEditSession;

    QFutureWatcher<application::Result<QImage>> m_pageLoadWatcher;
    quint64 m_pageLoadToken{0};
    application::OperationId m_pageLoadOperation{};

    QHash<QString, ThumbnailTicket> m_thumbnailInFlight; // key: PageId string
    QVector<domain::PageId> m_thumbnailQueue;
    QHash<QString, QFutureWatcher<application::Result<QImage>>*> m_thumbnailWatchers;

    QFutureWatcher<application::Result<application::SaveProjectResult>> m_saveWatcher;
    quint64 m_saveToken{0};
    application::OperationId m_saveOperation{};

    EditorProcessingController* m_processingController{nullptr};
};

} // namespace manga::ui::screen::editor
