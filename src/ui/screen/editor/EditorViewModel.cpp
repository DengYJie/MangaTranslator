#include "ui/screen/editor/EditorViewModel.h"

#include "application/mapping/ProjectMapper.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "application/usecase/region/RegionUseCases.h"
#include "application/usecase/thumbnail/ThumbnailUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "ui/command/PageClearLayerCommand.h"
#include "ui/command/ProcessingAdoptionCommands.h"
#include "ui/command/RegionCommands.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include <QDebug>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

namespace manga::ui::screen::editor {

namespace {

/// Applies one Inspector style field value to a layout copy. Returns false for
/// non-style fields and out-of-range enum payloads; range checks on numeric values
/// are delegated to TextLayout::isValid by the caller.
bool applyStyleFieldValue(domain::TextLayout& layout, InspectorField field, const QVariant& value) {
    switch (field) {
        case InspectorField::FontFamily: {
            const auto family = value.toString().trimmed();
            if (family.isEmpty()) {
                return false;
            }
            layout.fontFamily = family.toStdString();
            return true;
        }
        case InspectorField::FontWeight:
            layout.fontWeight = value.toInt();
            return true;
        case InspectorField::FontSizePt:
            layout.fontSizePt = value.toDouble();
            layout.autoFit = false;
            return true;
        case InspectorField::AutoFit:
            layout.autoFit = value.toBool();
            return true;
        case InspectorField::LetterSpacingEm:
            layout.letterSpacingEm = value.toDouble();
            return true;
        case InspectorField::LineSpacing:
            layout.lineSpacing = value.toDouble();
            return true;
        case InspectorField::Orientation: {
            const int ordinal = value.toInt();
            if (ordinal < 0 || ordinal > static_cast<int>(domain::TextOrientation::Vertical)) {
                return false;
            }
            layout.orientation = static_cast<domain::TextOrientation>(ordinal);
            return true;
        }
        case InspectorField::HorizontalAlignment: {
            const int ordinal = value.toInt();
            if (ordinal < 0 || ordinal > static_cast<int>(domain::HorizontalAlignment::Justify)) {
                return false;
            }
            layout.alignment.horizontal = static_cast<domain::HorizontalAlignment>(ordinal);
            return true;
        }
        case InspectorField::VerticalAlignment: {
            const int ordinal = value.toInt();
            if (ordinal < 0 || ordinal > static_cast<int>(domain::VerticalAlignment::Bottom)) {
                return false;
            }
            layout.alignment.vertical = static_cast<domain::VerticalAlignment>(ordinal);
            return true;
        }
        case InspectorField::MarginTop:
            layout.margins.top = value.toDouble();
            return true;
        case InspectorField::MarginRight:
            layout.margins.right = value.toDouble();
            return true;
        case InspectorField::MarginBottom:
            layout.margins.bottom = value.toDouble();
            return true;
        case InspectorField::MarginLeft:
            layout.margins.left = value.toDouble();
            return true;
        case InspectorField::TextColorHex:
            layout.textColorHex = value.toString().toStdString();
            return true;
        case InspectorField::StrokeColorHex:
            layout.strokeColorHex = value.toString().toStdString();
            return true;
        case InspectorField::StrokeWidthPx:
            layout.strokeWidthPx = value.toDouble();
            return true;
        default:
            return false;
    }
}

} // namespace

EditorViewModel::EditorViewModel(application::IThumbnailStore* thumbnailStore,
                                 application::IPageImageLoader* pageImageLoader,
                                 application::OperationRegistry* operationRegistry,
                                 application::IProjectRepository* projectRepository,
                                 QObject* parent)
    : base::BaseViewModel<EditorViewModel, EditorState>(parent), m_thumbnailStore(thumbnailStore),
      m_pageImageLoader(pageImageLoader), m_operationRegistry(operationRegistry),
      m_projectRepository(projectRepository), m_textEditSession(std::make_unique<TextEditSession>()) {
    if (m_thumbnailStore && m_pageImageLoader) {
        m_thumbnailUseCase =
            std::make_unique<application::LoadOrGenerateThumbnailUseCase>(m_thumbnailStore, m_pageImageLoader);
    }
    if (m_pageImageLoader) {
        m_pageLoadUseCase = std::make_unique<application::LoadPageImageUseCase>(m_pageImageLoader);
    }

    m_undoRedoTimer.setSingleShot(true);
    connect(&m_undoRedoTimer, &QTimer::timeout, this, &EditorViewModel::processPendingUndoRedo);

    connect(&m_pageLoadWatcher,
            &QFutureWatcher<application::Result<QImage>>::finished,
            this,
            &EditorViewModel::handlePageLoadFinished);
    connect(&m_previewWatcher,
            &QFutureWatcher<application::Result<QImage>>::finished,
            this,
            &EditorViewModel::handlePreviewFinished);
    connect(&m_saveWatcher,
            &QFutureWatcher<application::Result<application::SaveProjectResult>>::finished,
            this,
            &EditorViewModel::handleSaveFinished);
    connect(&m_closeWatcher,
            &QFutureWatcher<application::Result<void>>::finished,
            this,
            &EditorViewModel::handleProjectCloseFinished);
    connect(&m_snapshotWatcher,
            &QFutureWatcher<application::Result<QVector<application::ExportPageSnapshot>>>::finished,
            this,
            &EditorViewModel::handleExportSnapshotFinished);
    connect(&m_exportWatcher,
            &QFutureWatcher<application::Result<application::ExportResponse>>::finished,
            this,
            &EditorViewModel::handleExportFinished);
    connect(&m_appendPagesWatcher,
            &QFutureWatcher<application::Result<application::AppendPagesReport>>::finished,
            this,
            &EditorViewModel::handleAppendPagesFinished);
}

EditorViewModel::~EditorViewModel() {
    cancelThumbnailRequests();
    if (m_processingController && m_processingController->viewModel() == this) {
        m_processingController->setViewModel(nullptr);
    }
    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
    }
    if (m_operationRegistry && m_previewOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_previewOperation);
    }
    if (m_operationRegistry && m_appendPagesOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_appendPagesOperation);
    }
    cancelExport();
}

void EditorViewModel::openProjectSession(application::OpenedProjectSession session) {
    if (m_closeInProgress) {
        m_pendingSession = std::move(session);
        return;
    }
    if (!m_thumbnailStore || !m_pageImageLoader || !m_operationRegistry) {
        updateState([&](EditorState& s) {
            EditorNotice notice;
            notice.safeMessage = QStringLiteral("Internal error: ports not initialized");
            notice.retryable = false;
            s.notice = notice;
        });
        return;
    }

    cancelThumbnailRequests();
    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
        m_pageLoadOperation = application::OperationId{};
    }
    cancelExport();
    invalidateRequests();

    m_session = application::mapping::ProjectMapper::toSessionDescriptor(session);
    m_session.warnings = session.warnings;
    m_openedSession = std::move(session);
    m_hasSession = true;
    m_viewTransforms.clear();

    // Create undo infrastructure after aggregate is stored.
    m_undoStack = std::make_unique<QUndoStack>();
    m_commandContext = std::make_unique<command::EditorCommandContext>(&m_openedSession->project);
    m_undoHistoryFaulted = false;
    m_textEditSession->reset();

    updateState([this](EditorState& s) {
        s.projectId = m_session.projectId;
        s.projectName = m_session.projectName;
        s.pages.clear();
        for (const auto& pageDesc : m_session.pages) {
            EditorPageEntry entry;
            entry.id = pageDesc.id;
            entry.orderIndex = pageDesc.orderIndex;
            entry.displayName = QFileInfo(pageDesc.originalAssetPath).fileName();
            entry.imageSize = pageDesc.imageSize;
            entry.regionCount = pageDesc.regionCount;
            s.pages.append(entry);
        }

        s.projectSessionGeneration++;
        s.viewGeneration++;
        s.notice.reset();
        s.isPageLoading = false;
        s.currentPageId = domain::PageId{};
        s.isExporting = false;
        s.selectedPageIds.clear();

        // Reset region selection and tool state for new session.
        s.regions.clear();
        s.selectedRegionIds.clear();
        s.primaryRegionId = domain::RegionId{};
        s.activeTool = CanvasTool::Select;
        s.previewMode = PreviewMode::Translated;
        s.inspectorMode = InspectorMode::None;
        updateInspectorProjection(s);
        s.canUndo = false;
        s.canRedo = false;
        s.saveState = DocumentSaveState{};
        if (m_openedSession) {
            s.saveState.isDirty = m_openedSession->project.isDirty();
        }
    });

    Q_EMIT thumbnailsInvalidated();

    if (!m_state.pages.isEmpty()) {
        selectPage(m_state.pages.first().id);
    }
}

void EditorViewModel::closeProjectSession() {
    if (m_closeInProgress || !m_hasSession || !m_openedSession) {
        return;
    }
    m_closeInProgress = true;
    cancelThumbnailRequests();
    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
        m_pageLoadOperation = application::OperationId{};
    }
    cancelExport();
    if (m_operationRegistry && m_appendPagesOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_appendPagesOperation);
        m_appendPagesOperation = application::OperationId{};
    }
    invalidateRequests();

    // Project-owned tasks are stopped here, before the session is torn down below.
    cancelProjectTasks();

    // Keep the aggregate alive until repository close completes. The repository
    // task may still access its session-owned paths and data during close.
    if (m_projectRepository && m_operationRegistry) {
        application::OperationDescriptor descriptor{
            .stage = application::OperationStage::LoadImage,
            .projectId = m_session.projectId,
            .displayTarget = tr("关闭项目"),
            .cancellable = false,
        };
        auto context = m_operationRegistry->createOperation(
            std::move(descriptor), m_state.projectSessionGeneration, m_state.viewGeneration);
        application::CloseProjectUseCase useCase(m_projectRepository);
        application::CloseProjectRequest request;
        request.projectId = m_session.projectId;
        request.projectRoot = m_session.projectRootPath;
        m_closeWatcher.setFuture(useCase.execute(request, context));
        return;
    }

    completeProjectClose();
}

void EditorViewModel::handleProjectCloseFinished() {
    if (!m_closeInProgress) {
        return;
    }
    const auto result = m_closeWatcher.result();
    if (!result.has_value()) {
        m_closeInProgress = false;
        const auto presentation = m_recoveryPresenter.present(result.error());
        updateState([&](EditorState& s) {
            EditorNotice notice;
            if (presentation) {
                notice.safeMessage = presentation->safeMessage;
                notice.actionText = presentation->actionText;
                notice.action = presentation->action;
                notice.retryable = presentation->action == application::RecoveryAction::Retry;
            } else {
                notice.safeMessage = result.error().safeMessage;
                notice.action = application::RecoveryAction::Retry;
                notice.retryable = true;
            }
            s.notice = notice;
        });
        return;
    }

    completeProjectClose();
}

void EditorViewModel::completeProjectClose() {
    if (m_processingController != nullptr) {
        m_processingController->reset();
    }

    m_undoStack.reset();
    m_commandContext.reset();
    m_undoHistoryFaulted = false;
    m_textEditSession->reset();

    m_hasSession = false;
    m_session = application::ProjectSessionDescriptor{};
    m_openedSession.reset();
    m_viewTransforms.clear();

    const auto projGen = m_state.projectSessionGeneration + 1;
    const auto viewGen = m_state.viewGeneration + 1;
    const bool isOcrAvailable = m_state.isOcrAvailable;
    const QString ocrUnavailableReason = m_state.ocrUnavailableReason;
    const bool isTranslationAvailable = m_state.isTranslationAvailable;
    const QString translationUnavailableReason = m_state.translationUnavailableReason;
    const bool isInpaintAvailable = m_state.isInpaintAvailable;
    const QString inpaintUnavailableReason = m_state.inpaintUnavailableReason;
    updateState([&](EditorState& s) {
        s = EditorState{};
        s.projectSessionGeneration = projGen;
        s.viewGeneration = viewGen;
        s.isOcrAvailable = isOcrAvailable;
        s.ocrUnavailableReason = ocrUnavailableReason;
        s.isTranslationAvailable = isTranslationAvailable;
        s.translationUnavailableReason = translationUnavailableReason;
        s.isInpaintAvailable = isInpaintAvailable;
        s.inpaintUnavailableReason = inpaintUnavailableReason;
    });

    Q_EMIT thumbnailsInvalidated();
    m_closeInProgress = false;
    Q_EMIT projectClosed();
    if (m_pendingSession) {
        auto pendingSession = std::move(*m_pendingSession);
        m_pendingSession.reset();
        openProjectSession(std::move(pendingSession));
    }
}

void EditorViewModel::selectPage(const domain::PageId& pageId) {
    if (!m_hasSession || pageId == m_state.currentPageId || findPage(pageId) == nullptr) {
        return;
    }

    sealActiveTextSession();

    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
        m_pageLoadOperation = application::OperationId{};
    }
    m_pageLoadToken = beginRequest();

    updateState([&](EditorState& s) {
        s.currentPageId = pageId;
        s.isPageLoading = true;
        s.notice.reset();
        s.viewGeneration++;

        // Page switch clears region selection.
        s.selectedRegionIds.clear();
        s.primaryRegionId = domain::RegionId{};
        s.inspectorMode = InspectorMode::None;
        updateInspectorProjection(s);
    });

    rebuildCurrentPageProjection();

    if (m_processingController != nullptr) {
        m_processingController->handlePageSwitched(pageId);
    }

    // Thumbnail work belongs to the project session and survives page navigation.
    startPageLoad(pageId);
}

void EditorViewModel::selectAdjacentPage(int offset) {
    if (!m_hasSession || m_state.pages.isEmpty()) {
        return;
    }
    int currentIndex = -1;
    for (int i = 0; i < m_state.pages.size(); ++i) {
        if (m_state.pages[i].id == m_state.currentPageId) {
            currentIndex = i;
            break;
        }
    }
    if (currentIndex == -1 && !m_state.pages.isEmpty()) {
        selectPage(m_state.pages.first().id);
        return;
    }
    int newIndex = currentIndex + offset;
    if (newIndex >= 0 && newIndex < m_state.pages.size()) {
        selectPage(m_state.pages[newIndex].id);
    }
}

void EditorViewModel::retryPageLoad() {
    if (m_hasSession && m_state.currentPageId.isValid() && m_state.notice.has_value() && m_state.notice->retryable) {
        const auto pageId = m_state.currentPageId;
        updateState([&](EditorState& s) {
            s.notice.reset();
            s.isPageLoading = true;
        });
        startPageLoad(pageId);
    }
}

void EditorViewModel::requestThumbnail(const domain::PageId& pageId, bool force) {
    if (!m_thumbnailStore || !m_pageImageLoader || !m_operationRegistry) {
        updateState([&](EditorState& s) {
            EditorNotice notice;
            notice.safeMessage = QStringLiteral("Internal error: ports not initialized");
            notice.retryable = false;
            s.notice = notice;
        });
        return;
    }

    if (!m_hasSession || !findPage(pageId)) {
        return;
    }

    const QString key = QString::fromStdString(pageId.toString());
    const bool inFlight = m_thumbnailInFlight.contains(key);
    const bool inQueue = m_thumbnailQueueSet.contains(key);

    if ((inFlight || inQueue) && !force) {
        return;
    }
    if (force && inFlight) {
        return; // Don't stack requests
    }

    if (!inFlight) {
        if (m_thumbnailInFlight.size() >= kMaxConcurrentThumbnails) {
            if (!inQueue) {
                m_thumbnailQueue.enqueue(pageId);
                m_thumbnailQueueSet.insert(key);
            }
        } else {
            if (inQueue) {
                m_thumbnailQueue.removeOne(pageId);
                m_thumbnailQueueSet.remove(key);
            }
            startThumbnail(pageId);
        }
    }
}

void EditorViewModel::setVisiblePages(const QVector<domain::PageId>& pageIds) {
    m_visiblePageIds.clear();
    m_visiblePageIds.reserve(pageIds.size());
    for (const auto& pageId : pageIds) {
        m_visiblePageIds.insert(QString::fromStdString(pageId.toString()));
    }
}

void EditorViewModel::noteViewTransform(double zoom, const QPointF& centerOnPage) {
    if (m_hasSession && m_state.currentPageId.isValid()) {
        const double clampedZoom = std::clamp(zoom, 0.05, 16.0);
        ViewTransform transform;
        transform.zoom = clampedZoom;
        transform.centerOnPage = centerOnPage;
        m_viewTransforms.insert(QString::fromStdString(m_state.currentPageId.toString()), transform);

        if (qAbs(m_state.zoomFactor - clampedZoom) > 0.001) {
            updateState([clampedZoom](EditorState& s) { s.zoomFactor = clampedZoom; });
        }
    }
}

void EditorViewModel::dismissNotice() {
    if (m_state.notice.has_value()) {
        updateState([&](EditorState& s) { s.notice.reset(); });
    }
}

void EditorViewModel::setActiveTool(CanvasTool tool) {
    if (!m_hasSession) {
        return;
    }
    // Uncommitted canvas gestures are cancelled here once gesture support exists.
    updateState([&](EditorState& s) { s.activeTool = tool; });
}

void EditorViewModel::selectRegion(const domain::RegionId& regionId, RegionSelectionMode mode) {
    if (!m_hasSession || !m_state.currentPageId.isValid()) {
        return;
    }

    // When clicking empty space (invalid/empty regionId), Replace mode means "deselect all".
    if (!regionId.isValid()) {
        if (mode == RegionSelectionMode::Replace && !m_state.selectedRegionIds.isEmpty()) {
            sealActiveTextSession();
            updateState([&](EditorState& s) {
                s.selectedRegionIds.clear();
                s.primaryRegionId = domain::RegionId{};
                s.inspectorMode = InspectorMode::None;
                updateInspectorProjection(s);
            });
        }
        return;
    }

    // Verify the region exists on the current page.
    bool found = false;
    for (const auto& rp : m_state.regions) {
        if (rp.id == regionId) {
            found = true;
            break;
        }
    }
    if (!found) {
        return; // Unknown region id is ignored.
    }

    sealActiveTextSession();

    updateState([&](EditorState& s) {
        switch (mode) {
            case RegionSelectionMode::Replace:
                s.selectedRegionIds.clear();
                s.selectedRegionIds.append(regionId);
                s.primaryRegionId = regionId;
                break;
            case RegionSelectionMode::Toggle: {
                const int idx = s.selectedRegionIds.indexOf(regionId);
                if (idx >= 0) {
                    s.selectedRegionIds.removeAt(idx);
                    // If toggling off the primary, pick the first remaining or nil.
                    if (s.primaryRegionId == regionId) {
                        s.primaryRegionId =
                            s.selectedRegionIds.isEmpty() ? domain::RegionId{} : s.selectedRegionIds.first();
                    }
                } else {
                    s.selectedRegionIds.append(regionId);
                    s.primaryRegionId = regionId;
                }
                break;
            }
            case RegionSelectionMode::Range: {
                // Range selects contiguous span in region order between current primary and target.
                if (!s.primaryRegionId.isValid()) {
                    // No primary yet, treat as Replace.
                    s.selectedRegionIds.clear();
                    s.selectedRegionIds.append(regionId);
                    s.primaryRegionId = regionId;
                    break;
                }
                // Find order indices of primary and target.
                int primaryOrder = -1;
                int targetOrder = -1;
                for (const auto& rp : s.regions) {
                    if (rp.id == s.primaryRegionId) {
                        primaryOrder = rp.orderIndex;
                    }
                    if (rp.id == regionId) {
                        targetOrder = rp.orderIndex;
                    }
                }
                if (primaryOrder < 0 || targetOrder < 0) {
                    break;
                }
                const int lo = std::min(primaryOrder, targetOrder);
                const int hi = std::max(primaryOrder, targetOrder);
                s.selectedRegionIds.clear();
                for (const auto& rp : s.regions) {
                    if (rp.orderIndex >= lo && rp.orderIndex <= hi) {
                        s.selectedRegionIds.append(rp.id);
                    }
                }
                // Primary stays unchanged during range selection.
                break;
            }
        }

        // Sort selected IDs by page region order.
        std::sort(s.selectedRegionIds.begin(),
                  s.selectedRegionIds.end(),
                  [&s](const domain::RegionId& a, const domain::RegionId& b) {
                      int orderA = -1;
                      int orderB = -1;
                      for (const auto& rp : s.regions) {
                          if (rp.id == a)
                              orderA = rp.orderIndex;
                          if (rp.id == b)
                              orderB = rp.orderIndex;
                      }
                      return orderA < orderB;
                  });

        // Derive inspector mode from selection count.
        s.inspectorMode = s.selectedRegionIds.size() > 1
                              ? InspectorMode::Multi
                              : (s.selectedRegionIds.size() == 1 ? InspectorMode::Single : InspectorMode::None);
        if (s.selectedRegionIds.isEmpty()) {
            s.primaryRegionId = domain::RegionId{};
        }
        updateInspectorProjection(s);
    });
}

void EditorViewModel::clearRegionSelection() {
    if (!m_hasSession) {
        return;
    }
    sealActiveTextSession();
    updateState([&](EditorState& s) {
        s.selectedRegionIds.clear();
        s.primaryRegionId = domain::RegionId{};
        s.inspectorMode = InspectorMode::None;
        updateInspectorProjection(s);
    });
}

void EditorViewModel::selectAllRegionsOnCurrentPage() {
    if (!m_hasSession || !m_state.currentPageId.isValid()) {
        return;
    }
    sealActiveTextSession();
    updateState([&](EditorState& s) {
        s.selectedRegionIds.clear();
        for (const auto& rp : s.regions) {
            s.selectedRegionIds.append(rp.id);
        }
        if (!s.selectedRegionIds.isEmpty()) {
            s.primaryRegionId = s.selectedRegionIds.first();
            s.inspectorMode = s.selectedRegionIds.size() > 1 ? InspectorMode::Multi : InspectorMode::Single;
        } else {
            s.primaryRegionId = domain::RegionId{};
            s.inspectorMode = InspectorMode::None;
        }
        updateInspectorProjection(s);
    });
}

void EditorViewModel::setPreviewMode(PreviewMode mode) {
    if (!m_hasSession) {
        return;
    }
    updateState([&](EditorState& s) { s.previewMode = mode; });
}

void EditorViewModel::requestPreview(const domain::PageId& pageId) {
    if (!m_hasSession || !m_openedSession || !m_operationRegistry || !m_derivedAssetStore ||
        pageId != m_state.currentPageId) {
        return;
    }
    if (!findPage(pageId)) {
        return;
    }

    quint64 contentRevision = 0;
    for (const auto& region : m_state.regions) {
        contentRevision = contentRevision * 1315423911u + region.contentRevision;
    }
    const auto* page = m_openedSession->project.findPage(pageId);
    if (page != nullptr) {
        if (page->clearLayerAsset().has_value()) {
            contentRevision = contentRevision * 1315423911u + qHash(QString::fromStdString(*page->clearLayerAsset()));
        }
        if (page->manualOverrideLayerAsset().has_value()) {
            contentRevision =
                contentRevision * 1315423911u + qHash(QString::fromStdString(*page->manualOverrideLayerAsset()));
        }
    }
    const quint64 sessionGeneration = m_state.projectSessionGeneration;
    if (m_previewPageId == pageId && m_previewSessionGeneration == sessionGeneration &&
        m_previewContentRevision == contentRevision &&
        (m_previewOperation.isValid() || !m_cachedPreviewImage.isNull())) {
        return;
    }

    if (m_previewOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_previewOperation);
    }
    ++m_previewToken;
    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::LoadImage,
        .projectId = m_session.projectId,
        .pageId = pageId,
        .displayTarget = tr("预览渲染"),
        .cancellable = true,
    };
    const auto context =
        m_operationRegistry->createOperation(std::move(descriptor), sessionGeneration, m_state.viewGeneration);
    m_previewOperation = context.operationId;
    m_previewPageId = pageId;
    m_previewSessionGeneration = sessionGeneration;
    m_previewContentRevision = contentRevision;
    m_cachedPreviewImage = QImage{};
    m_cachedClearImage = QImage{};
    m_cachedManualOverrideImage = QImage{};
    Q_EMIT previewImageReady(PageImageDelivery{pageId, QImage{}});

    ExportSnapshotBuilder builder(&m_openedSession->project, m_pageImageLoader, m_derivedAssetStore);
    ExportSnapshotBuilder::Request request;
    request.projectRoot = m_session.projectRootPath;
    request.pageIds = {pageId};
    m_previewWatcher.setFuture(builder.executePreview(request, context));
}

void EditorViewModel::invalidatePreviewAndRequestCurrentPage() {
    if (m_operationRegistry && m_previewOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_previewOperation);
    }
    m_previewOperation = application::OperationId{};
    ++m_previewToken;
    m_previewPageId = domain::PageId{};
    m_previewSessionGeneration = 0;
    m_previewContentRevision = 0;
    m_cachedPreviewImage = QImage{};
    m_cachedClearImage = QImage{};
    m_cachedManualOverrideImage = QImage{};
    if (m_hasSession && m_state.currentPageId.isValid()) {
        requestPreview(m_state.currentPageId);
    }
}

void EditorViewModel::handlePreviewFinished() {
    const quint64 token = m_previewToken;
    if (token == 0 || m_previewWatcher.isCanceled()) {
        return;
    }
    auto result = m_previewWatcher.result();
    m_previewOperation = application::OperationId{};
    quint64 currentRevision = 0;
    if (m_state.currentPageId == m_previewPageId) {
        for (const auto& region : m_state.regions) {
            currentRevision = currentRevision * 1315423911u + region.contentRevision;
        }
        const auto* page = m_openedSession->project.findPage(m_previewPageId);
        if (page != nullptr) {
            if (page->clearLayerAsset().has_value()) {
                currentRevision =
                    currentRevision * 1315423911u + qHash(QString::fromStdString(*page->clearLayerAsset()));
            }
            if (page->manualOverrideLayerAsset().has_value()) {
                currentRevision =
                    currentRevision * 1315423911u + qHash(QString::fromStdString(*page->manualOverrideLayerAsset()));
            }
        }
    }
    if (!result.has_value() || token != m_previewToken || !m_hasSession ||
        m_state.projectSessionGeneration != m_previewSessionGeneration || m_state.currentPageId != m_previewPageId ||
        currentRevision != m_previewContentRevision) {
        return;
    }
    m_cachedPreviewImage = result.value().compositeImage;
    m_cachedClearImage = result.value().clearImage;
    m_cachedManualOverrideImage = result.value().manualOverrideImage;
    Q_EMIT previewImageReady(PageImageDelivery{m_previewPageId, m_cachedPreviewImage});
    Q_EMIT clearImageReady(PageImageDelivery{m_previewPageId, m_cachedClearImage});
    Q_EMIT manualOverrideReady(
        ManualOverrideDelivery{m_previewPageId, m_cachedManualOverrideImage, result.value().manualOverrideRef});
}

bool EditorViewModel::removeRegionForTest(const domain::RegionId& regionId) {
    if (!m_hasSession || !m_openedSession || !m_state.currentPageId.isValid()) {
        return false;
    }

    application::RemoveRegionUseCase useCase(&m_openedSession->project);
    application::RemoveRegionRequest request;
    request.pageId = m_state.currentPageId;
    request.regionId = regionId;

    auto result = useCase.execute(request);
    if (!result.has_value()) {
        return false;
    }

    rebuildCurrentPageProjection();

    // Update save state after mutation.
    updateState([this](EditorState& s) { s.saveState.isDirty = m_openedSession->project.isDirty(); });

    return true;
}

void EditorViewModel::rebuildCurrentPageProjection() {
    if (!m_hasSession || !m_openedSession || !m_state.currentPageId.isValid()) {
        return;
    }

    const auto* page = m_openedSession->project.findPage(m_state.currentPageId);
    if (!page) {
        return;
    }

    QVector<RegionProjection> projections;
    projections.reserve(static_cast<int>(page->regions().size()));
    for (const auto& region : page->regions()) {
        RegionProjection rp;
        rp.id = region.id();
        rp.geometry = region.geometry();
        rp.bubbleGeometry = region.bubbleGeometry();
        rp.bubblePolygon = region.bubblePolygon();
        rp.effectiveTranslation = QString::fromStdString(region.translation().effectiveText());
        rp.layout = region.layout();
        rp.isEnabled = region.isEnabled();
        rp.isTranslationVisible = region.isTranslationVisible();
        rp.orderIndex = region.orderIndex();
        rp.contentRevision = region.contentRevision();
        rp.isLocked = region.isLocked();
        rp.isCleared = region.isCleared();
        rp.hasManualTranslation = region.translation().hasManualText();
        projections.append(rp);
    }

    // Sort by orderIndex.
    std::sort(projections.begin(), projections.end(), [](const RegionProjection& a, const RegionProjection& b) {
        return a.orderIndex < b.orderIndex;
    });

    // Sanitize selection: drop IDs no longer present in the page.
    auto isValidId = [&projections](const domain::RegionId& id) {
        for (const auto& rp : projections) {
            if (rp.id == id) {
                return true;
            }
        }
        return false;
    };

    QVector<domain::RegionId> sanitizedSelection;
    for (const auto& id : m_state.selectedRegionIds) {
        if (isValidId(id)) {
            sanitizedSelection.append(id);
        }
    }

    domain::RegionId sanitizedPrimary = m_state.primaryRegionId;
    if (!sanitizedPrimary.isValid() || !isValidId(sanitizedPrimary)) {
        sanitizedPrimary = sanitizedSelection.isEmpty() ? domain::RegionId{} : sanitizedSelection.first();
    }

    InspectorMode mode = sanitizedSelection.size() > 1
                             ? InspectorMode::Multi
                             : (sanitizedSelection.size() == 1 ? InspectorMode::Single : InspectorMode::None);

    updateState([&](EditorState& s) {
        s.regions = projections;
        s.selectedRegionIds = sanitizedSelection;
        s.primaryRegionId = sanitizedPrimary;
        s.inspectorMode = mode;
        updateInspectorProjection(s);
    });
}

void EditorViewModel::updateInspectorProjection(EditorState& s) const {
    if (!m_hasSession || !m_openedSession || !s.currentPageId.isValid() || s.selectedRegionIds.isEmpty()) {
        s.inspector = std::monostate{};
        s.inspectorMode = InspectorMode::None;
        return;
    }

    const auto* page = m_openedSession->project.findPage(s.currentPageId);
    if (page == nullptr) {
        s.inspector = std::monostate{};
        s.inspectorMode = InspectorMode::None;
        return;
    }

    if (s.selectedRegionIds.size() > 1) {
        MultiRegionInspectorState multi;
        multi.pageId = page->id();
        multi.regionIds = s.selectedRegionIds;
        multi.regionCount = s.selectedRegionIds.size();
        multi.allLocked = true;

        bool first = true;
        for (const auto& regionId : s.selectedRegionIds) {
            const auto* region = page->findRegion(regionId);
            if (region == nullptr) {
                continue;
            }
            const auto& layout = region->layout();
            const QString fontFamily = QString::fromStdString(layout.fontFamily);
            const QString textColorHex = QString::fromStdString(layout.textColorHex);
            const QString strokeColorHex = QString::fromStdString(layout.strokeColorHex);

            if (first) {
                multi.fontFamily = fontFamily;
                multi.fontWeight = layout.fontWeight;
                multi.fontSizePt = layout.fontSizePt;
                multi.letterSpacingEm = layout.letterSpacingEm;
                multi.orientation = layout.orientation;
                multi.horizontalAlignment = layout.alignment.horizontal;
                multi.verticalAlignment = layout.alignment.vertical;
                multi.lineSpacing = layout.lineSpacing;
                multi.marginTop = layout.margins.top;
                multi.marginRight = layout.margins.right;
                multi.marginBottom = layout.margins.bottom;
                multi.marginLeft = layout.margins.left;
                multi.textColorHex = textColorHex;
                multi.strokeColorHex = strokeColorHex;
                multi.strokeWidthPx = layout.strokeWidthPx;
                multi.autoFit = layout.autoFit;
                first = false;
            } else {
                if (multi.fontFamily.has_value() && *multi.fontFamily != fontFamily) {
                    multi.fontFamily.reset();
                }
                if (multi.fontWeight.has_value() && *multi.fontWeight != layout.fontWeight) {
                    multi.fontWeight.reset();
                }
                if (multi.fontSizePt.has_value() && *multi.fontSizePt != layout.fontSizePt) {
                    multi.fontSizePt.reset();
                }
                if (multi.letterSpacingEm.has_value() && *multi.letterSpacingEm != layout.letterSpacingEm) {
                    multi.letterSpacingEm.reset();
                }
                if (multi.orientation.has_value() && *multi.orientation != layout.orientation) {
                    multi.orientation.reset();
                }
                if (multi.horizontalAlignment.has_value() &&
                    *multi.horizontalAlignment != layout.alignment.horizontal) {
                    multi.horizontalAlignment.reset();
                }
                if (multi.verticalAlignment.has_value() && *multi.verticalAlignment != layout.alignment.vertical) {
                    multi.verticalAlignment.reset();
                }
                if (multi.lineSpacing.has_value() && *multi.lineSpacing != layout.lineSpacing) {
                    multi.lineSpacing.reset();
                }
                if (multi.marginTop.has_value() && *multi.marginTop != layout.margins.top) {
                    multi.marginTop.reset();
                }
                if (multi.marginRight.has_value() && *multi.marginRight != layout.margins.right) {
                    multi.marginRight.reset();
                }
                if (multi.marginBottom.has_value() && *multi.marginBottom != layout.margins.bottom) {
                    multi.marginBottom.reset();
                }
                if (multi.marginLeft.has_value() && *multi.marginLeft != layout.margins.left) {
                    multi.marginLeft.reset();
                }
                if (multi.textColorHex.has_value() && *multi.textColorHex != textColorHex) {
                    multi.textColorHex.reset();
                }
                if (multi.strokeColorHex.has_value() && *multi.strokeColorHex != strokeColorHex) {
                    multi.strokeColorHex.reset();
                }
                if (multi.strokeWidthPx.has_value() && *multi.strokeWidthPx != layout.strokeWidthPx) {
                    multi.strokeWidthPx.reset();
                }
                if (multi.autoFit.has_value() && *multi.autoFit != layout.autoFit) {
                    multi.autoFit.reset();
                }
            }
            multi.allLocked = multi.allLocked && region->isLocked();
        }

        if (first) {
            s.inspector = std::monostate{};
            s.inspectorMode = InspectorMode::None;
            return;
        }

        s.inspector = multi;
        s.inspectorMode = InspectorMode::Multi;
        return;
    }

    const auto* region = page->findRegion(s.selectedRegionIds.first());
    if (region == nullptr) {
        s.inspector = std::monostate{};
        s.inspectorMode = InspectorMode::None;
        return;
    }

    const auto& layout = region->layout();
    const auto& translation = region->translation();

    SingleRegionInspectorState single;
    single.pageId = page->id();
    single.regionId = region->id();
    single.sourceText = QString::fromStdString(region->source().text);
    single.sourceType = region->source().sourceType;
    single.sourceConfidence = region->source().confidence;
    single.hasManualTranslation = translation.hasManualText();
    if (translation.manualText().has_value()) {
        single.manualTranslation = QString::fromStdString(*translation.manualText());
    }
    single.effectiveTranslation = QString::fromStdString(translation.effectiveText());
    single.candidates.reserve(static_cast<int>(translation.candidates().size()));
    for (const auto& candidate : translation.candidates()) {
        InspectorCandidateProjection projection;
        projection.id = QString::fromStdString(candidate.id);
        projection.text = QString::fromStdString(candidate.text);
        projection.confidence = candidate.confidence;
        projection.provider = QString::fromStdString(candidate.provider);
        projection.isSelected =
            translation.selectedCandidateId().has_value() && *translation.selectedCandidateId() == candidate.id;
        single.candidates.append(projection);
    }
    single.isLocked = region->isLocked();
    single.isCleared = region->isCleared();
    single.geometry = region->geometry();
    single.orderIndex = region->orderIndex();
    single.fontFamily = QString::fromStdString(layout.fontFamily);
    single.fontSizePt = layout.fontSizePt;
    single.fontWeight = layout.fontWeight;
    single.letterSpacingEm = layout.letterSpacingEm;
    single.orientation = layout.orientation;
    single.horizontalAlignment = layout.alignment.horizontal;
    single.verticalAlignment = layout.alignment.vertical;
    single.lineSpacing = layout.lineSpacing;
    single.margins = layout.margins;
    single.textColorHex = QString::fromStdString(layout.textColorHex);
    single.strokeColorHex = QString::fromStdString(layout.strokeColorHex);
    single.strokeWidthPx = layout.strokeWidthPx;
    single.autoFit = layout.autoFit;

    s.inspector = single;
    s.inspectorMode = InspectorMode::Single;
}

void EditorViewModel::startPageLoad(const domain::PageId& pageId) {
    m_currentPageImage = QImage{};
    if (!m_pageLoadUseCase || !m_operationRegistry) {
        updateState([&](EditorState& s) {
            EditorNotice notice;
            notice.safeMessage = QStringLiteral("Internal error: ports not initialized");
            notice.retryable = false;
            s.notice = notice;
            s.isPageLoading = false;
        });
        return;
    }

    const auto* page = findPage(pageId);
    if (!page) {
        return;
    }

    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::LoadImage,
        .projectId = m_session.projectId,
        .pageId = pageId,
        .displayTarget = tr("页面图像"),
        .cancellable = true,
    };
    auto context = m_operationRegistry->createOperation(
        std::move(descriptor), m_state.projectSessionGeneration, m_state.viewGeneration);
    m_pageLoadOperation = context.operationId;

    application::LoadPageImageRequest request;
    request.projectId = m_session.projectId;
    request.projectRoot = m_session.projectRootPath;
    request.pageId = pageId;
    request.assetPath = page->originalAssetPath;
    request.expectedSize = page->imageSize;

    m_pageLoadWatcher.setFuture(m_pageLoadUseCase->execute(request, context));
}

void EditorViewModel::handlePageLoadFinished() {
    if (!isRequestCurrent(m_pageLoadToken)) {
        return;
    }

    auto result = m_pageLoadWatcher.result();
    m_pageLoadOperation = application::OperationId{};

    if (result.has_value()) {
        m_currentPageImage = result.value();
        PageImageDelivery delivery;
        delivery.pageId = m_state.currentPageId;
        delivery.image = result.value();

        const QString key = QString::fromStdString(delivery.pageId.toString());
        if (m_viewTransforms.contains(key)) {
            const auto& t = m_viewTransforms.value(key);
            delivery.hasSavedTransform = true;
            delivery.zoom = t.zoom;
            delivery.centerOnPage = t.centerOnPage;
        } else {
            delivery.hasSavedTransform = false;
        }
        updateState([&](EditorState& s) { s.isPageLoading = false; });
        Q_EMIT pageImageReady(delivery);
        requestPreview(m_state.currentPageId);
    } else {
        const auto& error = result.error();
        updateState([&](EditorState& s) {
            s.isPageLoading = false;
            if (error.category != application::ErrorCategory::Cancelled) {
                const auto presentation =
                    m_recoveryPresenter.present(error, QString::fromStdString(m_state.currentPageId.toString()));
                if (presentation) {
                    EditorNotice notice;
                    notice.safeMessage = presentation->safeMessage;
                    notice.retryable = true;
                    s.notice = notice;
                }
            }
        });
    }
}

void EditorViewModel::startThumbnail(const domain::PageId& pageId) {
    if (!m_thumbnailUseCase || !m_operationRegistry) {
        return;
    }

    const auto* page = findPage(pageId);
    if (!page) {
        return;
    }

    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::LoadImage,
        .projectId = m_session.projectId,
        .pageId = pageId,
        .displayTarget = tr("缩略图"),
        .cancellable = true,
    };
    auto context = m_operationRegistry->createOperation(std::move(descriptor), m_state.projectSessionGeneration, 0);
    if (context.progressReporter) {
        context.progressReporter->markRunning();
    }

    ThumbnailTicket ticket;
    ticket.operationId = context.operationId;
    ticket.projectSessionGeneration = m_state.projectSessionGeneration;
    const QString key = QString::fromStdString(pageId.toString());
    m_thumbnailInFlight.insert(key, ticket);

    application::LoadOrGenerateThumbnailRequest request;
    request.projectId = m_session.projectId;
    request.projectRoot = m_session.projectRootPath;
    request.pageId = pageId;
    request.originalAssetPath = page->originalAssetPath;
    request.originalSize = QSize(page->imageSize.width, page->imageSize.height);
    request.targetSize = QSize(kThumbnailEdgePx, kThumbnailEdgePx); // Must cover the list cell at device pixel ratio

    auto* watcher = new QFutureWatcher<application::Result<QImage>>(this);
    m_thumbnailWatchers.insert(key, watcher);

    connect(watcher, &QFutureWatcher<application::Result<QImage>>::finished, this, [this, pageId, watcher, context]() {
        handleThumbnailFinished(pageId, watcher, context);
    });

    watcher->setFuture(m_thumbnailUseCase->execute(request, context));
}

void EditorViewModel::handleThumbnailFinished(const domain::PageId& pageId,
                                              QFutureWatcher<application::Result<QImage>>* watcher,
                                              const application::OperationContext& context) {
    watcher->deleteLater();
    const QString key = QString::fromStdString(pageId.toString());

    const auto pending = m_thumbnailInFlight.constFind(key);
    if (pending == m_thumbnailInFlight.cend() || pending->operationId != context.operationId ||
        m_thumbnailWatchers.value(key) != watcher) {
        return; // Already cancelled or handled
    }

    const auto ticket = m_thumbnailInFlight.take(key);
    m_thumbnailWatchers.remove(key);
    auto result = watcher->result();
    if (context.stopToken.stop_requested()) {
        result = std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                   QStringLiteral("cancelled.user_requested"),
                                                                   tr("缩略图加载已取消，点击重试")));
    } else if (result && result->isNull()) {
        result = std::unexpected(application::makeApplicationError(
            application::ErrorCategory::Input, QStringLiteral("thumbnail.empty_image"), tr("缩略图为空，点击重试")));
    }
    if (context.progressReporter) {
        context.progressReporter->markFinished(result ? std::nullopt
                                                      : std::optional<application::ApplicationError>(result.error()));
    }

    if (m_hasSession && ticket.projectSessionGeneration == m_state.projectSessionGeneration && findPage(pageId)) {
        if (result.has_value()) {
            Q_EMIT thumbnailReady(pageId, result.value());
        } else {
            const auto& error = result.error();
            Q_EMIT thumbnailFailed(pageId, error.isCancelled() ? tr("缩略图加载已取消，点击重试") : error.safeMessage);
        }
    }

    pumpThumbnailQueue();
}

void EditorViewModel::pumpThumbnailQueue() {
    while (!m_thumbnailQueue.isEmpty() && m_thumbnailInFlight.size() < kMaxConcurrentThumbnails) {
        startThumbnail(takeNextQueuedPageId());
    }
}

domain::PageId EditorViewModel::takeNextQueuedPageId() {
    if (m_thumbnailQueue.isEmpty()) {
        return domain::PageId{};
    }

    const auto takeAt = [this](int index) {
        const domain::PageId pageId = m_thumbnailQueue.takeAt(index);
        m_thumbnailQueueSet.remove(QString::fromStdString(pageId.toString()));
        return pageId;
    };

    for (int i = 0; i < m_thumbnailQueue.size(); ++i) {
        const QString key = QString::fromStdString(m_thumbnailQueue.at(i).toString());
        if (m_visiblePageIds.contains(key)) {
            return takeAt(i);
        }
    }

    // Resolving each queued page through pageOrder would scan the whole session once per queue entry.
    // The index is local because the session page list is replaced wholesale in several places, and a
    // stale member index would silently reorder thumbnail loading.
    std::unordered_map<std::string, int> orderById;
    orderById.reserve(static_cast<std::size_t>(m_session.pages.size()));
    for (int i = 0; i < m_session.pages.size(); ++i) {
        orderById.emplace(m_session.pages.at(i).id.toString(), i);
    }
    const auto orderOf = [&orderById](const domain::PageId& pageId) {
        const auto found = orderById.find(pageId.toString());
        return found == orderById.end() ? -1 : found->second;
    };

    const int currentOrder = orderOf(m_state.currentPageId);
    int bestIndex = 0;
    int bestDistance = -1;
    for (int i = 0; i < m_thumbnailQueue.size(); ++i) {
        const int order = orderOf(m_thumbnailQueue.at(i));
        if (order < 0 || currentOrder < 0) {
            continue;
        }
        const int distance = qAbs(order - currentOrder);
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return takeAt(bestIndex);
}

void EditorViewModel::cancelThumbnailRequests() {
    if (m_operationRegistry) {
        for (const auto& ticket : std::as_const(m_thumbnailInFlight)) {
            m_operationRegistry->cancelOperation(ticket.operationId);
        }
    }
    m_thumbnailInFlight.clear();
    m_thumbnailQueue.clear();
    m_thumbnailQueueSet.clear();
    m_visiblePageIds.clear();

    for (auto* watcher : m_thumbnailWatchers) {
        watcher->disconnect();
        watcher->deleteLater();
    }
    m_thumbnailWatchers.clear();
}

const application::PageDescriptor* EditorViewModel::findPage(const domain::PageId& pageId) const {
    for (const auto& page : m_session.pages) {
        if (page.id == pageId) {
            return &page;
        }
    }
    return nullptr;
}

int EditorViewModel::pageOrder(const domain::PageId& pageId) const {
    for (int i = 0; i < m_session.pages.size(); ++i) {
        if (m_session.pages.at(i).id == pageId) {
            return i;
        }
    }
    return -1;
}

void EditorViewModel::detectRegionsOnCurrentPage() {
    if (!m_hasSession || !m_state.currentPageId.isValid() || m_currentPageImage.isNull()) {
        return;
    }
    if (m_processingController != nullptr) {
        // Currently hardcoded to true since this is the global re-detection entry point.
        // If a dialog is needed here, it should be lifted to the view layer.
        m_processingController->detectRegions(m_state.currentPageId, true);
    }
}

void EditorViewModel::applyDetectedRegions(const domain::PageId& pageId,
                                           const QVector<application::DetectedRegionItem>& detected,
                                           bool clearExisting) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || pageId != m_state.currentPageId || detected.isEmpty()) {
        return;
    }

    const auto* page = m_openedSession->project.findPage(pageId);
    if (page == nullptr) {
        return;
    }

    sealActiveTextSession();

    // When clearExisting is requested, group the delete-all and the batch-create into one
    // macro command so a single Undo reverts the entire re-detection in one step.
    const bool hasMacro = clearExisting && !page->regions().empty();
    if (hasMacro) {
        m_undoStack->beginMacro(tr("重新检测区域"));

        // Build delete entries for all existing regions on this page.
        QVector<command::DeleteRegionsCommand::RegionEntry> deleteEntries;
        deleteEntries.reserve(static_cast<int>(page->regions().size()));
        for (const auto& region : page->regions()) {
            command::DeleteRegionsCommand::RegionEntry entry;
            entry.region = region;
            entry.orderIndex = region.orderIndex();
            deleteEntries.append(entry);
        }
        std::sort(deleteEntries.begin(), deleteEntries.end(), [](const auto& a, const auto& b) {
            return a.orderIndex < b.orderIndex;
        });

        auto* deleteCmd = new command::DeleteRegionsCommand(m_commandContext.get(), pageId, deleteEntries);
        m_undoStack->push(deleteCmd);
        if (deleteCmd->hasFaulted()) {
            m_undoHistoryFaulted = true;
            m_undoStack->endMacro();
            rebuildCurrentPageProjection();
            refreshUndoState();
            return;
        }
    }

    // Re-read the page after the potential delete so orderIndex starts from the new size.
    const auto* freshPage = m_openedSession->project.findPage(pageId);
    const int startOrderIndex = freshPage ? static_cast<int>(freshPage->regions().size()) : 0;
    const auto pageSize = m_commandContext->pageSize(pageId);

    QVector<command::CreateRegionsBatchCommand::Entry> entries;
    QVector<domain::RegionId> newRegionIds;
    entries.reserve(detected.size());
    newRegionIds.reserve(detected.size());

    int orderOffset = 0;
    for (const auto& item : detected) {
        if (!item.rect.isValid() || !item.rect.isWithinBounds(pageSize)) {
            continue;
        }
        if (item.rect.width < canvas::kMinRegionDimension || item.rect.height < canvas::kMinRegionDimension) {
            continue;
        }

        const auto regionId = domain::RegionId::generate();
        entries.append(command::CreateRegionsBatchCommand::Entry{.regionId = regionId,
                                                                 .geometry = item.rect,
                                                                 .bubbleGeometry = item.bubbleRect,
                                                                 .bubblePolygon = item.bubblePolygon,
                                                                 .orderIndex = startOrderIndex + orderOffset++});
        newRegionIds.append(regionId);
    }

    if (entries.isEmpty()) {
        if (hasMacro) {
            m_undoStack->endMacro();
        }
        rebuildCurrentPageProjection();
        refreshUndoState();
        return;
    }

    auto* cmd = new command::CreateRegionsBatchCommand(m_commandContext.get(), pageId, std::move(entries));
    m_undoStack->push(cmd);
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    if (hasMacro) {
        m_undoStack->endMacro();
    }

    updateState([&newRegionIds](EditorState& s) {
        s.selectedRegionIds = newRegionIds;
        s.primaryRegionId = newRegionIds.isEmpty() ? domain::RegionId{} : newRegionIds.first();
        s.inspectorMode = newRegionIds.size() > 1
                              ? InspectorMode::Multi
                              : (newRegionIds.size() == 1 ? InspectorMode::Single : InspectorMode::None);
    });
    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::createPointText(QPointF anchor) {
    if (!m_hasSession || !m_commandContext || !m_state.currentPageId.isValid()) {
        return;
    }
    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    if (!pageSize.isValid() || anchor.x() < 0.0 || anchor.y() < 0.0 || anchor.x() >= pageSize.width ||
        anchor.y() >= pageSize.height) {
        return;
    }

    constexpr double maxWidth = 320.0;
    constexpr double maxHeight = 120.0;
    const double width = std::min(maxWidth, static_cast<double>(pageSize.width) - anchor.x());
    const double height = std::min(maxHeight, static_cast<double>(pageSize.height) - anchor.y());
    if (width < domain::RectGeometry::kMinDimension || height < domain::RectGeometry::kMinDimension) {
        return;
    }
    domain::RectGeometry geometry{anchor.x(), anchor.y(), width, height};
    domain::TextLayout layout;
    layout.autoFit = true;
    layout.alignment.horizontal = domain::HorizontalAlignment::Left;
    layout.alignment.vertical = domain::VerticalAlignment::Top;
    createTextRegion(geometry, std::move(layout));
}

void EditorViewModel::createTextBox(const domain::RectGeometry& geometry) {
    if (!m_hasSession || !m_commandContext || !m_state.currentPageId.isValid()) {
        return;
    }
    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    if (!geometry.isValid() || !geometry.isWithinBounds(pageSize)) {
        return;
    }
    domain::TextLayout layout;
    layout.autoFit = true;
    createTextRegion(geometry, std::move(layout));
}

void EditorViewModel::createRegion(const domain::RectGeometry& geometry, domain::TextLayout layout) {
    createTextRegion(geometry, std::move(layout));
}

void EditorViewModel::createTextRegion(const domain::RectGeometry& geometry, domain::TextLayout layout) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || !m_state.currentPageId.isValid()) {
        return;
    }

    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    if (!geometry.isValid() || !geometry.isWithinBounds(pageSize)) {
        return;
    }
    if (geometry.width < domain::RectGeometry::kMinDimension || geometry.height < domain::RectGeometry::kMinDimension ||
        !layout.isValid()) {
        return;
    }

    const auto regionId = domain::RegionId::generate();
    const auto* page = m_openedSession->project.findPage(m_state.currentPageId);
    const int orderIndex = page ? static_cast<int>(page->regions().size()) : 0;

    sealActiveTextSession();
    auto* cmd = new command::CreateTextRegionCommand(
        m_commandContext.get(), m_state.currentPageId, regionId, geometry, std::move(layout), orderIndex);
    m_undoStack->push(cmd);
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
        rebuildCurrentPageProjection();
        refreshUndoState();
        return;
    }

    updateState([&](EditorState& s) {
        s.selectedRegionIds = {regionId};
        s.primaryRegionId = regionId;
        s.inspectorMode = InspectorMode::Single;
        updateInspectorProjection(s);
    });
    rebuildCurrentPageProjection();
    refreshUndoState();
    Q_EMIT textRegionCreated(regionId);
}

void EditorViewModel::deleteRegions(const QVector<domain::RegionId>& regionIds) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || !m_state.currentPageId.isValid() || regionIds.isEmpty()) {
        return;
    }

    // Capture snapshots sorted by orderIndex.
    QVector<command::DeleteRegionsCommand::RegionEntry> entries;
    const auto* page = m_openedSession->project.findPage(m_state.currentPageId);
    if (page == nullptr) {
        return;
    }
    for (const auto& id : regionIds) {
        const auto* region = page->findRegion(id);
        if (region != nullptr) {
            command::DeleteRegionsCommand::RegionEntry entry;
            entry.region = *region;
            entry.orderIndex = region->orderIndex();
            entries.append(entry);
        }
    }
    if (entries.isEmpty()) {
        return;
    }

    // Sort by orderIndex ascending.
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.orderIndex < b.orderIndex; });

    sealActiveTextSession();
    auto* cmd = new command::DeleteRegionsCommand(m_commandContext.get(), m_state.currentPageId, entries);
    m_undoStack->push(cmd);
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::commitRegionGeometry(const domain::RegionId& regionId,
                                           const domain::RectGeometry& newGeometry,
                                           canvas::EditKind kind) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || !m_state.currentPageId.isValid()) {
        return;
    }

    const auto beforeOpt = m_commandContext->regionGeometry(m_state.currentPageId, regionId);
    if (!beforeOpt.has_value()) {
        return;
    }
    const auto& before = *beforeOpt;

    // No-op if geometry is identical.
    if (before == newGeometry) {
        return;
    }

    // Validate new geometry.
    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    if (!newGeometry.isValid() || !newGeometry.isWithinBounds(pageSize)) {
        return;
    }

    sealActiveTextSession();
    auto* cmd = new command::RegionGeometryCommand(
        m_commandContext.get(), m_state.currentPageId, regionId, before, newGeometry, kind);
    m_undoStack->push(cmd);

    // Check for fault after push (redo was called automatically).
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::commitRegionGeometries(const QVector<domain::RegionId>& regionIds,
                                             const QVector<domain::RectGeometry>& newGeometries,
                                             canvas::EditKind kind) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || !m_state.currentPageId.isValid() || regionIds.isEmpty() ||
        regionIds.size() != newGeometries.size()) {
        return;
    }

    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    QVector<command::RegionGeometryBatchCommand::Entry> entries;
    entries.reserve(regionIds.size());
    for (int i = 0; i < regionIds.size(); ++i) {
        const auto before = m_commandContext->regionGeometry(m_state.currentPageId, regionIds.at(i));
        if (!before.has_value() || !newGeometries.at(i).isValid() || !newGeometries.at(i).isWithinBounds(pageSize)) {
            return;
        }
        if (*before != newGeometries.at(i)) {
            entries.append({regionIds.at(i), *before, newGeometries.at(i)});
        }
    }
    if (entries.isEmpty()) {
        return;
    }

    sealActiveTextSession();
    auto* cmd = new command::RegionGeometryBatchCommand(m_commandContext.get(), m_state.currentPageId, entries, kind);
    m_undoStack->push(cmd);
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }
    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::sealActiveTextSession() {
    if (m_textEditSession) {
        m_textEditSession->seal();
    }
}

void EditorViewModel::sealTextEditSession() {
    sealActiveTextSession();
}

void EditorViewModel::prepareForSave() {
    sealActiveTextSession();
}

void EditorViewModel::pushPropertyCommand(command::RegionPropertyCommand* cmd) {
    m_undoStack->push(cmd);
    // A merged push deletes cmd; read fault state from the surviving stack top.
    const auto* top = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index() - 1));
    if (top != nullptr && top->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::commitInspectorFieldEdit(const domain::PageId& pageId,
                                               const QVector<domain::RegionId>& regionIds,
                                               InspectorField field,
                                               const QVariant& value) {
    qInfo().noquote() << "[EditorViewModel] commitInspectorFieldEdit: field=" << static_cast<int>(field)
                      << "value=" << value << "targetRegions=" << regionIds.size()
                      << "currentPage=" << QString::fromStdString(m_state.currentPageId.toString())
                      << "pageId=" << QString::fromStdString(pageId.toString());

    if (!m_hasSession || !m_undoStack || !m_commandContext || regionIds.isEmpty() || pageId != m_state.currentPageId) {
        qWarning().noquote() << "[EditorViewModel] commitInspectorFieldEdit ignored: hasSession=" << m_hasSession
                             << "undoStack=" << (m_undoStack != nullptr)
                             << "commandContext=" << (m_commandContext != nullptr)
                             << "regionIdsEmpty=" << regionIds.isEmpty()
                             << "pageMatches=" << (pageId == m_state.currentPageId);
        return;
    }

    switch (field) {
        case InspectorField::SourceText:
        case InspectorField::ManualTranslation:
            if (regionIds.size() == 1 && value.canConvert<QString>()) {
                commitTextEdit(pageId, regionIds.first(), field, value.toString());
            }
            return;
        case InspectorField::Cleared:
            // The clear lifecycle uses its own two-phase command path; a plain
            // property command cannot carry the immutable clear assets.
            return;
        default:
            commitStyleEdit(pageId, regionIds, field, value);
            return;
    }
}

void EditorViewModel::commitTextEdit(const domain::PageId& pageId,
                                     const domain::RegionId& regionId,
                                     InspectorField field,
                                     const QString& text) {
    const auto* page = m_openedSession->project.findPage(pageId);
    const auto* region = page != nullptr ? page->findRegion(regionId) : nullptr;
    if (region == nullptr || region->isLocked()) {
        return;
    }

    domain::PageRegionEdit before;
    before.regionId = regionId;
    domain::PageRegionEdit after;
    after.regionId = regionId;
    command::RegionPropertyField propertyField;

    if (field == InspectorField::SourceText) {
        if (region->source().text == text.toStdString()) {
            return;
        }
        before.source = region->source();
        auto next = region->source();
        // setManualText preserves provenance: OCR text becomes OcrManualModified.
        next.setManualText(text.toStdString());
        after.source = std::move(next);
        propertyField = command::RegionPropertyField::SourceText;
    } else {
        if (region->translation().manualText().value_or(std::string()) == text.toStdString()) {
            return;
        }
        before.translation = region->translation();
        auto next = region->translation();
        next.setManualText(text.toStdString());
        after.translation = std::move(next);
        propertyField = command::RegionPropertyField::ManualTranslation;
    }

    const auto key = m_textEditSession->keyFor(m_openedSession->project.id(), pageId, regionId, propertyField);
    auto* cmd = new command::RegionPropertyCommand(
        m_commandContext.get(), key, m_textEditSession->nowMs(), before, after, QStringLiteral("Edit Text"));
    pushPropertyCommand(cmd);
}

void EditorViewModel::commitStyleEdit(const domain::PageId& pageId,
                                      const QVector<domain::RegionId>& regionIds,
                                      InspectorField field,
                                      const QVariant& value) {
    const auto* page = m_openedSession->project.findPage(pageId);
    if (page == nullptr) {
        qWarning().noquote() << "[EditorViewModel] commitStyleEdit: page not found"
                             << QString::fromStdString(pageId.toString());
        return;
    }

    QVector<command::RegionPropertyCommand::PropertyEdit> edits;
    for (const auto& regionId : regionIds) {
        const auto* region = page->findRegion(regionId);
        // One missing or locked target vetoes the whole batch before anything is pushed.
        if (region == nullptr || region->isLocked()) {
            qWarning().noquote() << "[EditorViewModel] commitStyleEdit vetoed: region missing or locked:"
                                 << QString::fromStdString(regionId.toString());
            return;
        }
        auto layout = region->layout();
        const auto oldLayout = layout;
        if (!applyStyleFieldValue(layout, field, value)) {
            qWarning().noquote() << "[EditorViewModel] applyStyleFieldValue failed for field="
                                 << static_cast<int>(field) << "value=" << value;
            return;
        }
        if (!layout.isValid()) {
            qWarning().noquote() << "[EditorViewModel] layout.isValid() failed after field=" << static_cast<int>(field)
                                 << "value=" << value << "weight=" << layout.fontWeight
                                 << "fontSize=" << layout.fontSizePt << "letterSpacing=" << layout.letterSpacingEm
                                 << "textColor=" << QString::fromStdString(layout.textColorHex)
                                 << "strokeColor=" << QString::fromStdString(layout.strokeColorHex)
                                 << "strokeWidth=" << layout.strokeWidthPx << "lineSpacing=" << layout.lineSpacing
                                 << "marginsValid=" << layout.margins.isValid();
            return;
        }
        if (layout == region->layout()) {
            qInfo().noquote() << "[EditorViewModel] layout unchanged for region"
                              << QString::fromStdString(regionId.toString());
            continue;
        }
        qInfo().noquote() << "[EditorViewModel] style edit staged: region="
                          << QString::fromStdString(regionId.toString()) << "oldWeight=" << oldLayout.fontWeight
                          << "newWeight=" << layout.fontWeight
                          << "oldOrient=" << static_cast<int>(oldLayout.orientation)
                          << "newOrient=" << static_cast<int>(layout.orientation)
                          << "oldFamily=" << QString::fromStdString(oldLayout.fontFamily)
                          << "newFamily=" << QString::fromStdString(layout.fontFamily);
        command::RegionPropertyCommand::PropertyEdit entry;
        entry.regionId = regionId;
        entry.before.regionId = regionId;
        entry.before.layout = region->layout();
        entry.after.regionId = regionId;
        entry.after.layout = layout;
        edits.append(std::move(entry));
    }
    if (edits.isEmpty()) {
        qInfo().noquote() << "[EditorViewModel] commitStyleEdit: no edits to push";
        return;
    }

    sealActiveTextSession();
    auto* cmd = new command::RegionPropertyCommand(m_commandContext.get(), pageId, edits, QStringLiteral("Edit Style"));
    pushPropertyCommand(cmd);
    qInfo().noquote() << "[EditorViewModel] pushed RegionPropertyCommand for" << edits.size() << "regions";
}

void EditorViewModel::adoptCandidate(const domain::PageId& pageId,
                                     const domain::RegionId& regionId,
                                     const QString& candidateId) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || pageId != m_state.currentPageId) {
        return;
    }
    const auto* page = m_openedSession->project.findPage(pageId);
    const auto* region = page != nullptr ? page->findRegion(regionId) : nullptr;
    if (region == nullptr || region->isLocked()) {
        return;
    }

    const auto candidate = candidateId.toStdString();
    if (!region->translation().hasCandidate(candidate)) {
        return;
    }

    command::RegionPropertyCommand::PropertyEdit entry;
    entry.regionId = regionId;
    entry.before.regionId = regionId;
    entry.before.translation = region->translation();
    entry.after = entry.before;
    entry.after.translation->selectCandidate(candidate);
    if (*entry.before.translation == *entry.after.translation) {
        return; // Already the selected candidate.
    }

    sealActiveTextSession();
    auto* cmd = new command::RegionPropertyCommand(m_commandContext.get(),
                                                   pageId,
                                                   QVector<command::RegionPropertyCommand::PropertyEdit>{entry},
                                                   QStringLiteral("Adopt Candidate"));
    pushPropertyCommand(cmd);
}

void EditorViewModel::pushPreparedCommand(QUndoCommand* cmd) {
    if (!m_hasSession || !m_undoStack) {
        delete cmd;
        return;
    }
    sealActiveTextSession();
    m_undoStack->push(cmd);

    const auto* top = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index() - 1));
    if (top != nullptr && top->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }
    rebuildCurrentPageProjection();
    refreshUndoState();
}

application::Result<void>
EditorViewModel::adoptProcessingProposal(const application::PreparedProcessingAdoption& prepared) {
    if (!m_hasSession || !m_undoStack) {
        return std::unexpected(application::detail::applyError(
            application::ErrorCategory::Internal, "internal.no_session", "No active session"));
    }

    // The proposal is revalidated against the live aggregate inside this same GUI event, immediately
    // before the command is pushed, so no other edit can land between the check and the push.
    domain::PageId targetPageId;
    QVector<domain::RegionId> targetRegionIds;
    if (!prepared.changes.isEmpty()) {
        targetPageId = prepared.changes.first().pageId;
        for (const auto& c : prepared.changes)
            targetRegionIds.append(c.regionId);
    } else if (prepared.clearAssetChange) {
        targetPageId = prepared.clearAssetChange->pageId;
        targetRegionIds.append(prepared.clearAssetChange->regionId);
    }

    for (const auto& change : prepared.changes) {
        const auto* page = m_openedSession->project.findPage(change.pageId);
        if (!page) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Project, "project.page_not_found", "Page not found"));
        }
        const auto* region = page->findRegion(change.regionId);
        if (!region) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Project, "project.region_not_found", "Region not found"));
        }
        if (region->isLocked()) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Input, "input.manual_override", "Region is locked"));
        }

        // A moved content revision means the region was edited after the prepare step, so the
        // prepared before state no longer describes it.
        if (region->contentRevision() != change.expectedRevision) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Input, "input.stale_revision", "Stale revision"));
        }
    }

    if (prepared.clearAssetChange) {
        const auto& c = *prepared.clearAssetChange;
        const auto* page = m_openedSession->project.findPage(c.pageId);
        if (!page) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Project, "project.page_not_found", "Page not found"));
        }
        const auto* region = page->findRegion(c.regionId);
        // The recorded cleared flag and the content revision must both still match the prepared
        // before state, or the clear no longer applies to this region.
        if (!region || region->isLocked() || region->isCleared() != c.beforeState ||
            region->contentRevision() != c.expectedRevision) {
            return std::unexpected(application::detail::applyError(
                application::ErrorCategory::Input, "input.stale_revision", "Stale clear state"));
        }
    }

    sealActiveTextSession();

    if (targetPageId.isValid() && targetPageId != m_state.currentPageId) {
        selectPage(targetPageId);
    }

    auto* cmd = new command::ProcessingAdoptionCommand(m_commandContext.get(), prepared);
    m_undoStack->push(cmd);

    if (!targetRegionIds.isEmpty()) {
        updateState([&](EditorState& s) {
            s.selectedRegionIds = targetRegionIds;
            s.primaryRegionId = targetRegionIds.first();
            s.inspectorMode = targetRegionIds.size() > 1 ? InspectorMode::Multi : InspectorMode::Single;
            updateInspectorProjection(s);
        });
    }

    const auto* top =
        dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index() - 1));
    if (top != nullptr && top->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
    return {};
}

void EditorViewModel::rejectProcessingProposal([[maybe_unused]] application::OperationId operationId) {}

void EditorViewModel::cancelProjectTasks() {
    if (!m_hasSession) {
        return;
    }
    // Session-owned workflows hold non-owning pointers into the aggregate. Requesting stop here is
    // what makes it safe for completeProjectClose to destroy the aggregate afterwards.
    if (m_operationRegistry != nullptr && m_session.projectId.isValid()) {
        m_operationRegistry->cancelProject(m_session.projectId);
    }
    const auto viewGen = m_state.viewGeneration + 1;
    updateState([&](EditorState& s) { s.viewGeneration = viewGen; });
    Q_EMIT thumbnailsInvalidated();
}

void EditorViewModel::undo() {
    if (!m_hasSession || !m_undoStack || m_undoHistoryFaulted) {
        return;
    }
    if (!m_undoStack->canUndo()) {
        return;
    }
    m_pendingUndoCount++;
    if (!m_undoRedoTimer.isActive()) {
        m_undoRedoTimer.start(0);
    }
}

void EditorViewModel::redo() {
    if (!m_hasSession || !m_undoStack || m_undoHistoryFaulted) {
        return;
    }
    if (!m_undoStack->canRedo()) {
        return;
    }
    m_pendingRedoCount++;
    if (!m_undoRedoTimer.isActive()) {
        m_undoRedoTimer.start(0);
    }
}

void EditorViewModel::processPendingUndoRedo() {
    if (m_pendingUndoCount == 0 && m_pendingRedoCount == 0) {
        return;
    }
    if (!m_hasSession || !m_undoStack || m_undoHistoryFaulted) {
        m_pendingUndoCount = 0;
        m_pendingRedoCount = 0;
        return;
    }

    sealActiveTextSession();

    bool anyClearLayerChanged = false;
    domain::PageId targetPage = m_state.currentPageId;

    while (m_pendingUndoCount > 0 && m_undoStack->canUndo()) {
        const bool clearLayerChanged =
            dynamic_cast<const command::PageClearLayerCommand*>(m_undoStack->command(m_undoStack->index())) != nullptr;
        anyClearLayerChanged |= clearLayerChanged;

        m_undoStack->undo();

        const auto* affected = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index()));
        const auto* affectedAdoption =
            dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index()));

        if (affected != nullptr && affected->hasFaulted()) {
            m_undoHistoryFaulted = true;
        } else if (affected != nullptr && affected->pageId().isValid()) {
            targetPage = affected->pageId();
        } else if (affectedAdoption != nullptr && affectedAdoption->hasFaulted()) {
            m_undoHistoryFaulted = true;
        } else if (affectedAdoption != nullptr && affectedAdoption->pageId().isValid()) {
            targetPage = affectedAdoption->pageId();
        }

        m_pendingUndoCount--;
    }

    while (m_pendingRedoCount > 0 && m_undoStack->canRedo()) {
        const auto* pendingCmd =
            dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index()));
        const auto* pendingAdoption =
            dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index()));
        const bool clearLayerChanged =
            dynamic_cast<const command::PageClearLayerCommand*>(m_undoStack->command(m_undoStack->index())) != nullptr;

        anyClearLayerChanged |= clearLayerChanged;

        if (pendingCmd != nullptr) {
            targetPage = pendingCmd->pageId();
        } else if (pendingAdoption != nullptr) {
            targetPage = pendingAdoption->pageId();
        }

        m_undoStack->redo();

        const auto* done = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index() - 1));
        const auto* doneAdoption =
            dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index() - 1));

        if (done != nullptr && done->hasFaulted()) {
            m_undoHistoryFaulted = true;
        } else if (doneAdoption != nullptr && doneAdoption->hasFaulted()) {
            m_undoHistoryFaulted = true;
        }

        m_pendingRedoCount--;
    }

    m_pendingUndoCount = 0;
    m_pendingRedoCount = 0;

    if (targetPage.isValid() && targetPage != m_state.currentPageId) {
        selectPage(targetPage);
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
    if (anyClearLayerChanged) {
        invalidatePreviewAndRequestCurrentPage();
    }
}

void EditorViewModel::refreshUndoState() {
    updateState([this](EditorState& s) {
        if (m_undoStack) {
            s.canUndo = m_undoStack->canUndo();
            s.canRedo = m_undoStack->canRedo();
            s.saveState.undoText = m_undoStack->canUndo() ? m_undoStack->undoText() : QStringLiteral("Undo");
            s.saveState.redoText = m_undoStack->canRedo() ? m_undoStack->redoText() : QStringLiteral("Redo");
        } else {
            s.canUndo = false;
            s.canRedo = false;
            s.saveState.undoText = QStringLiteral("Undo");
            s.saveState.redoText = QStringLiteral("Redo");
        }
        s.undoHistoryFaulted = m_undoHistoryFaulted;
        if (m_openedSession) {
            s.saveState.isDirty = m_openedSession->project.isDirty();
        }
    });
}

void EditorViewModel::saveProject() {
    if (!m_hasSession || !m_openedSession || !m_projectRepository || !m_operationRegistry) {
        return;
    }

    if (m_saveOperation.isValid()) {
        return; // Already saving
    }

    prepareForSave();

    updateState([this](EditorState& s) { s.saveState.isSaving = true; });

    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::LoadImage,
        .projectId = m_session.projectId,
        .displayTarget = tr("保存项目"),
        .cancellable = false,
    };
    auto context = m_operationRegistry->createOperation(
        std::move(descriptor), m_state.projectSessionGeneration, m_state.viewGeneration);
    m_saveOperation = context.operationId;
    m_saveToken = beginRequest();

    application::SaveProjectUseCase useCase(m_projectRepository);
    application::SaveProjectRequest request;
    request.project = m_openedSession->project; // copy aggregate snapshot

    m_saveWatcher.setFuture(useCase.execute(request, context));
}

void EditorViewModel::handleSaveFinished() {
    if (!isRequestCurrent(m_saveToken)) {
        return;
    }

    auto result = m_saveWatcher.result();
    m_saveOperation = application::OperationId{};

    if (result.has_value()) {
        const auto& saveResult = result.value();
        bool cleanMarked = m_openedSession->project.markCleanIfRevision(saveResult.savedRevision);

        if (cleanMarked && m_undoStack) {
            m_undoStack->setClean();
        }

        updateState([this](EditorState& s) { s.saveState.isSaving = false; });

        refreshUndoState();
        Q_EMIT saveCompleted(QString::fromStdString(std::to_string(saveResult.savedRevision)));
    } else {
        updateState([this](EditorState& s) { s.saveState.isSaving = false; });
        Q_EMIT saveFailed(result.error());
    }
}

void EditorViewModel::markProjectCleanAtRevision(quint64 savedRevision) {
    if (!m_openedSession) {
        return;
    }
    if (!m_openedSession->project.markCleanIfRevision(savedRevision)) {
        return;
    }
    if (m_undoStack) {
        m_undoStack->setClean();
    }
    refreshUndoState();
    Q_EMIT saveCompleted(QString::number(savedRevision));
}

void EditorViewModel::setExportDependencies(application::IComicExporter* exporter,
                                            application::IDerivedAssetStore* derivedStore) {
    m_exporter = exporter;
    m_derivedAssetStore = derivedStore;
}

void EditorViewModel::setSelectedPages(const QVector<domain::PageId>& pageIds) {
    if (!m_hasSession) {
        return;
    }
    QVector<domain::PageId> valid;
    QSet<QString> seen;
    valid.reserve(pageIds.size());
    for (const auto& id : pageIds) {
        const auto key = QString::fromStdString(id.toString());
        if (!id.isValid() || seen.contains(key) || findPage(id) == nullptr) {
            continue;
        }
        seen.insert(key);
        valid.append(id);
    }
    if (m_state.selectedPageIds == valid) {
        return;
    }
    updateState([&](EditorState& s) { s.selectedPageIds = valid; });
}

void EditorViewModel::exportPages(application::ExportScope scope, const QString& outputDir) {
    if (!m_hasSession || !m_openedSession || !m_operationRegistry) {
        return;
    }
    if (m_exportOperation.isValid()) {
        return; // Already exporting
    }
    if (m_exporter == nullptr || m_derivedAssetStore == nullptr || m_pageImageLoader == nullptr) {
        failExport(application::makeApplicationError(application::ErrorCategory::Internal,
                                                     QStringLiteral("internal.null_dependency"),
                                                     QStringLiteral("Export dependency is unavailable")));
        return;
    }

    QVector<domain::PageId> targetPageIds;
    switch (scope) {
        case application::ExportScope::CurrentPage:
            if (!m_state.currentPageId.isValid()) {
                return;
            }
            targetPageIds.append(m_state.currentPageId);
            break;
        case application::ExportScope::SelectedPages:
            if (m_state.selectedPageIds.isEmpty()) {
                return;
            }
            targetPageIds = m_state.selectedPageIds;
            break;
        case application::ExportScope::AllPages:
            if (m_state.pages.isEmpty()) {
                return;
            }
            break;
    }

    // The descriptor target lists project page ordinals only: no content, no paths.
    QStringList ordinals;
    for (const auto& page : m_openedSession->project.pages()) {
        if (targetPageIds.isEmpty() || targetPageIds.contains(page.id())) {
            ordinals.append(QString::number(page.orderIndex() + 1));
        }
    }
    application::OperationDescriptor descriptor;
    descriptor.stage = application::OperationStage::Export;
    descriptor.projectId = m_session.projectId;
    descriptor.displayTarget = ordinals.join(QStringLiteral(", "));

    auto context =
        m_operationRegistry->createOperation(descriptor, m_state.projectSessionGeneration, m_state.viewGeneration);
    if (context.progressReporter) {
        context.progressReporter->setRange(0);
        context.progressReporter->setStatusText(tr("正在准备导出页面"));
        context.progressReporter->markRunning();
    } else {
        m_operationRegistry->markRunning(context.operationId);
    }
    qInfo() << "[Export] Started operation=" << context.operationId.toString() << "pages=" << ordinals.size();
    m_exportContext = context;
    m_exportOperation = context.operationId;
    m_exportScope = scope;
    m_exportOutputDir = outputDir;
    // Capture the export targets at intent time: navigation and selection stay
    // enabled while the export runs and must not alter it.
    m_exportCurrentPageId = m_state.currentPageId;
    m_exportSelectedPageIds = m_state.selectedPageIds;
    m_exportToken = ++m_exportSequence;

    updateState([](EditorState& s) {
        s.isExporting = true;
        s.notice.reset();
        s.exportNotice.reset();
        s.exportFailureSummaries.clear();
    });

    // The snapshot chain reads pages on worker threads, so it must not touch the
    // live aggregate: a concurrent session close would free it under the chain.
    m_exportProjectSnapshot = std::make_unique<domain::ComicProject>(m_openedSession->project);

    ExportSnapshotBuilder builder(m_exportProjectSnapshot.get(), m_pageImageLoader, m_derivedAssetStore);
    ExportSnapshotBuilder::Request snapshotRequest;
    snapshotRequest.projectRoot = m_session.projectRootPath;
    snapshotRequest.pageIds = targetPageIds;
    m_snapshotWatcher.setFuture(builder.execute(snapshotRequest, context));
}

void EditorViewModel::retryExportInDirectory(const QString& outputDir) {
    if (!outputDir.trimmed().isEmpty()) {
        exportPages(m_exportScope, outputDir);
    }
}

void EditorViewModel::setAppendPagesWorkflow(application::AppendPagesWorkflow* workflow) {
    m_appendPagesWorkflow = workflow;
}

void EditorViewModel::importPagesIntoProject(const QVector<QString>& filePaths) {
    if (!m_hasSession || !m_openedSession || m_appendPagesWorkflow == nullptr || m_operationRegistry == nullptr) {
        return;
    }
    if (filePaths.isEmpty() || m_appendPagesOperation.isValid()) {
        return;
    }

    application::OperationDescriptor descriptor{
        .stage = application::OperationStage::Import,
        .projectId = m_session.projectId,
        .displayTarget = tr("导入页面"),
        .cancellable = true,
    };
    auto context = m_operationRegistry->createOperation(
        std::move(descriptor), m_state.projectSessionGeneration, m_state.viewGeneration);
    m_appendPagesOperation = context.operationId;
    m_appendPagesSessionGeneration = m_state.projectSessionGeneration;

    application::AppendPagesRequest request;
    request.projectId = m_session.projectId;
    request.projectRoot = m_session.projectRootPath;
    request.filePaths = filePaths;
    request.project = &m_openedSession->project;
    request.sessionGeneration = m_state.projectSessionGeneration;
    request.currentSessionGeneration = [this]() { return m_state.projectSessionGeneration; };

    m_appendPagesWatcher.setFuture(m_appendPagesWorkflow->execute(std::move(request), std::move(context)));
}

void EditorViewModel::handleAppendPagesFinished() {
    const quint64 sessionGen = m_appendPagesSessionGeneration;
    m_appendPagesOperation = application::OperationId{};
    m_appendPagesSessionGeneration = 0;

    // A stale completion belongs to a closed or replaced session; its aggregate
    // mutation was already rejected inside the workflow.
    if (!m_hasSession || !m_openedSession || sessionGen != m_state.projectSessionGeneration) {
        return;
    }

    const auto result = m_appendPagesWatcher.result();
    if (!result.has_value()) {
        if (result.error().category == application::ErrorCategory::Cancelled) {
            return;
        }
        const auto presentation = m_recoveryPresenter.present(result.error());
        updateState([&](EditorState& s) {
            EditorNotice notice;
            if (presentation) {
                notice.safeMessage = presentation->safeMessage;
                notice.actionText = presentation->actionText;
                notice.action = presentation->action;
                notice.retryable = presentation->action == application::RecoveryAction::Retry;
            } else {
                notice.safeMessage = result.error().safeMessage;
                notice.action = application::RecoveryAction::Retry;
                notice.retryable = true;
            }
            s.notice = notice;
        });
        return;
    }

    // The aggregate gained pages, so the session descriptor and the page list
    // projection are re-derived from it.
    m_session = application::mapping::ProjectMapper::toSessionDescriptor(*m_openedSession);
    updateState([this](EditorState& s) {
        s.pages.clear();
        for (const auto& pageDesc : m_session.pages) {
            EditorPageEntry entry;
            entry.id = pageDesc.id;
            entry.orderIndex = pageDesc.orderIndex;
            entry.displayName = QFileInfo(pageDesc.originalAssetPath).fileName();
            entry.imageSize = pageDesc.imageSize;
            entry.regionCount = pageDesc.regionCount;
            s.pages.append(entry);
        }
        s.saveState.isDirty = m_openedSession->project.isDirty();
    });

    if (result->projectSaved) {
        markProjectCleanAtRevision(result->savedRevision);
    }
    if (!result->pageIds.isEmpty()) {
        selectPage(result->pageIds.first());
    }
}

void EditorViewModel::cancelExport() {
    if (m_operationRegistry && m_exportOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_exportOperation);
    }
    m_exportOperation = application::OperationId{};
    m_exportContext = {};
    m_exportProjectSnapshot.reset();
    // Invalidate pending export completions so queued handlers never touch the
    // released aggregate snapshot.
    ++m_exportSequence;
}

void EditorViewModel::handleExportSnapshotFinished() {
    if (m_exportToken != m_exportSequence) {
        return;
    }

    auto result = m_snapshotWatcher.result();
    if (!result.has_value()) {
        failExport(result.error());
        return;
    }

    qInfo() << "[Export] Snapshots ready operation=" << m_exportOperation.toString() << "pages=" << result->size();

    application::ExportPagesRequest request;
    request.projectId = m_exportProjectSnapshot->id();
    request.scope = m_exportScope;
    request.outputDir = m_exportOutputDir;
    request.pageSnapshots = result.value();
    if (m_exportScope == application::ExportScope::CurrentPage) {
        request.currentPageId = m_exportCurrentPageId;
    } else if (m_exportScope == application::ExportScope::SelectedPages) {
        request.selectedPageIds = m_exportSelectedPageIds;
    }

    application::ExportPagesUseCase useCase(m_exporter, m_exportProjectSnapshot.get());
    m_exportWatcher.setFuture(useCase.execute(request, m_exportContext));
}

void EditorViewModel::handleExportFinished() {
    if (m_exportToken != m_exportSequence) {
        return;
    }

    auto result = m_exportWatcher.result();
    if (result.has_value()) {
        const auto& response = result.value();
        const std::optional<application::ApplicationError> error =
            response.failures.isEmpty()
                ? std::nullopt
                : std::optional<application::ApplicationError>(response.failures.constFirst().error);
        qInfo() << "[Export] Finished operation=" << m_exportOperation.toString()
                << "exported=" << response.exported.size() << "failed=" << response.failures.size();
        if (m_exportContext.progressReporter) {
            m_exportContext.progressReporter->setValue(response.exported.size() + response.failures.size());
            m_exportContext.progressReporter->setStatusText(error.has_value() ? error->safeMessage : tr("导出完成"));
            m_exportContext.progressReporter->markFinished(error);
        } else if (m_operationRegistry && m_exportOperation.isValid()) {
            m_operationRegistry->markFinished(m_exportOperation, error);
        }
        m_exportOperation = application::OperationId{};
        m_exportContext = {};
        m_exportProjectSnapshot.reset();
        updateState([&](EditorState& s) {
            s.isExporting = false;
            s.notice.reset();
            s.exportFailureSummaries.clear();
            for (const auto& failure : response.failures) {
                s.exportFailureSummaries.append(failure.error.safeMessage);
            }
            s.exportNotice.reset();
            if (!response.failures.isEmpty()) {
                const auto presentation = m_recoveryPresenter.present(response.failures.constFirst().error);
                if (presentation) {
                    EditorNotice notice;
                    notice.safeMessage = presentation->safeMessage;
                    notice.actionText = presentation->actionText;
                    notice.action = presentation->action;
                    notice.retryable = presentation->action == application::RecoveryAction::Retry;
                    s.exportNotice = notice;
                }
            }
        });
        Q_EMIT exportCompleted(response);
    } else {
        failExport(result.error());
    }
}

void EditorViewModel::failExport(const application::ApplicationError& error) {
    qWarning() << "[Export] Failed operation=" << m_exportOperation.toString() << "code=" << error.code;
    if (m_exportContext.progressReporter) {
        m_exportContext.progressReporter->setStatusText(error.safeMessage);
        m_exportContext.progressReporter->markFinished(error);
    } else if (m_operationRegistry && m_exportOperation.isValid()) {
        m_operationRegistry->markFinished(m_exportOperation, error);
    }
    m_exportOperation = application::OperationId{};
    m_exportContext = {};
    m_exportProjectSnapshot.reset();
    updateState([&](EditorState& s) {
        s.isExporting = false;
        s.exportFailureSummaries.clear();
        s.exportNotice.reset();
        if (error.category != application::ErrorCategory::Cancelled) {
            const auto presentation = m_recoveryPresenter.present(error);
            if (presentation) {
                EditorNotice notice;
                notice.safeMessage = presentation->safeMessage;
                notice.actionText = presentation->actionText;
                notice.action = presentation->action;
                notice.retryable = presentation->action == application::RecoveryAction::Retry;
                s.notice = notice;
            }
        }
    });
    Q_EMIT exportFailed(error);
}

void EditorViewModel::requestCloseProject() {
    if (!m_hasSession || !m_openedSession) {
        Q_EMIT projectClosed();
        return;
    }

    if (m_openedSession->project.isDirty()) {
        ProjectCloseSummary summary;
        summary.projectName = m_session.projectName;
        summary.isDirty = true;
        summary.safeMessage = QCoreApplication::translate("EditorViewModel", "项目有未保存的修改，是否保存并关闭？");
        Q_EMIT closeDecisionRequested(summary);
    } else {
        closeProjectSession();
    }
}

void EditorViewModel::confirmDiscardAndClose() {
    if (m_hasSession) {
        closeProjectSession();
    }
}

void EditorViewModel::cancelClose() {}

void EditorViewModel::setProcessingController(EditorProcessingController* controller) {
    m_processingController = controller;
}

bool EditorViewModel::applyPageInpaintChange(const application::PreparedPageInpaintChange& change) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || change.projectId != m_state.projectId ||
        change.expectedSessionGeneration != m_state.projectSessionGeneration ||
        change.pageId != m_state.currentPageId) {
        return false;
    }
    auto* command =
        new command::PageClearLayerCommand(m_commandContext.get(), change, [this](const domain::PageId& pageId) {
            if (pageId == m_state.currentPageId)
                rebuildCurrentPageProjection();
        });
    m_undoStack->push(command);
    if (command->hasFaulted()) {
        m_undoHistoryFaulted = true;
        refreshUndoState();
        return false;
    }
    rebuildCurrentPageProjection();
    refreshUndoState();
    invalidatePreviewAndRequestCurrentPage();
    return true;
}

bool EditorViewModel::applyPageManualOverrideChange(const application::PreparedPageManualOverrideChange& change) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || change.projectId != m_state.projectId ||
        change.expectedSessionGeneration != m_state.projectSessionGeneration ||
        change.pageId != m_state.currentPageId) {
        return false;
    }
    auto* command =
        new command::PageManualOverrideCommand(m_commandContext.get(), change, [this](const domain::PageId& pageId) {
            if (pageId == m_state.currentPageId)
                rebuildCurrentPageProjection();
        });
    m_undoStack->push(command);
    if (command->hasFaulted()) {
        m_undoHistoryFaulted = true;
        refreshUndoState();
        return false;
    }
    rebuildCurrentPageProjection();
    refreshUndoState();
    invalidatePreviewAndRequestCurrentPage();
    return true;
}

void EditorViewModel::setBrushColor(const QColor& color) {
    updateState([&color](EditorState& s) { s.brushColor = color; });
}

void EditorViewModel::setBrushDiameter(double diameter) {
    const double clamped = std::clamp(diameter, 1.0, 128.0);
    updateState([clamped](EditorState& s) { s.brushDiameter = clamped; });
}

void EditorViewModel::updateProcessingState(const EditorProcessingState& processingState) {
    updateState([&](EditorState& s) { s.processing = processingState; });
}

} // namespace manga::ui::screen::editor
