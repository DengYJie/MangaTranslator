#include "ui/screen/editor/EditorViewModel.h"

#include "application/mapping/ProjectMapper.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "application/usecase/region/RegionUseCases.h"
#include "application/usecase/thumbnail/ThumbnailUseCases.h"
#include "application/workflow/OperationRegistry.h"
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
        s.previewMode = PreviewMode::Original;
        s.inspectorMode = InspectorMode::Page;
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
    invalidateRequests();

    // P5-D 项目任务取消钩子
    cancelProjectTasks();

    // Keep the aggregate alive until repository close completes. The repository
    // task may still access its session-owned paths and data during close.
    if (m_projectRepository && m_operationRegistry) {
        auto context = m_operationRegistry->createOperation(m_state.projectSessionGeneration, m_state.viewGeneration);
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
    updateState([&](EditorState& s) {
        s = EditorState{};
        s.projectSessionGeneration = projGen;
        s.viewGeneration = viewGen;
        s.isOcrAvailable = isOcrAvailable;
        s.ocrUnavailableReason = ocrUnavailableReason;
        s.isTranslationAvailable = isTranslationAvailable;
        s.translationUnavailableReason = translationUnavailableReason;
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
        s.inspectorMode = InspectorMode::Page;
        updateInspectorProjection(s);
    });

    rebuildCurrentPageProjection();

    if (m_processingController != nullptr) {
        m_processingController->handlePageSwitched(pageId);
    }

    m_thumbnailQueue.clear();
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
        qWarning() << "[ThumbnailVM] rejected: thumbnail ports unavailable";
        updateState([&](EditorState& s) {
            EditorNotice notice;
            notice.safeMessage = QStringLiteral("Internal error: ports not initialized");
            notice.retryable = false;
            s.notice = notice;
        });
        return;
    }

    if (!m_hasSession || !findPage(pageId)) {
        qWarning().noquote() << "[ThumbnailVM] rejected page=" << QString::fromStdString(pageId.toString())
                             << "session=" << m_hasSession;
        return;
    }

    const QString key = QString::fromStdString(pageId.toString());
    const bool inFlight = m_thumbnailInFlight.contains(key);
    const bool inQueue = m_thumbnailQueue.contains(pageId);

    if ((inFlight || inQueue) && !force) {
        qInfo().noquote() << "[ThumbnailVM] duplicate suppressed page=" << key;
        return;
    }
    if (force && inFlight) {
        return; // Don't stack requests
    }

    if (!inFlight) {
        if (m_thumbnailInFlight.size() >= kMaxConcurrentThumbnails) {
            if (!inQueue) {
                m_thumbnailQueue.append(pageId);
            }
        } else {
            if (inQueue) {
                m_thumbnailQueue.removeAll(pageId);
            }
            startThumbnail(pageId);
        }
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
        if (s.selectedRegionIds.isEmpty()) {
            s.inspectorMode = InspectorMode::Page;
            s.primaryRegionId = domain::RegionId{};
        } else if (s.selectedRegionIds.size() == 1) {
            s.inspectorMode = InspectorMode::Single;
        } else {
            s.inspectorMode = InspectorMode::Multi;
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
        s.inspectorMode = InspectorMode::Page;
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
            s.inspectorMode = s.selectedRegionIds.size() == 1 ? InspectorMode::Single : InspectorMode::Multi;
        } else {
            s.primaryRegionId = domain::RegionId{};
            s.inspectorMode = InspectorMode::Page;
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
    const auto context = m_operationRegistry->createOperation(sessionGeneration, m_state.viewGeneration);
    m_previewOperation = context.operationId;
    m_previewPageId = pageId;
    m_previewSessionGeneration = sessionGeneration;
    m_previewContentRevision = contentRevision;
    m_cachedPreviewImage = QImage{};
    Q_EMIT previewImageReady(PageImageDelivery{pageId, QImage{}});

    ExportSnapshotBuilder builder(&m_openedSession->project, m_pageImageLoader, m_derivedAssetStore);
    ExportSnapshotBuilder::Request request;
    request.projectRoot = m_session.projectRootPath;
    request.pageIds = {pageId};
    m_previewWatcher.setFuture(builder.executePreview(request, context));
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
    }
    if (!result.has_value() || token != m_previewToken || !m_hasSession ||
        m_state.projectSessionGeneration != m_previewSessionGeneration || m_state.currentPageId != m_previewPageId ||
        currentRevision != m_previewContentRevision) {
        return;
    }
    m_cachedPreviewImage = result.value();
    Q_EMIT previewImageReady(PageImageDelivery{m_previewPageId, m_cachedPreviewImage});
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

    InspectorMode mode = InspectorMode::Page;
    if (sanitizedSelection.size() == 1) {
        mode = InspectorMode::Single;
    } else if (sanitizedSelection.size() > 1) {
        mode = InspectorMode::Multi;
    }

    updateState([&](EditorState& s) {
        s.regions = projections;
        s.selectedRegionIds = sanitizedSelection;
        s.primaryRegionId = sanitizedPrimary;
        s.inspectorMode = mode;
        updateInspectorProjection(s);
    });
}

void EditorViewModel::updateInspectorProjection(EditorState& s) const {
    if (!m_hasSession || !m_openedSession || !s.currentPageId.isValid()) {
        s.inspector = PageInspectorState{};
        return;
    }

    const auto* page = m_openedSession->project.findPage(s.currentPageId);
    if (page == nullptr) {
        s.inspector = PageInspectorState{};
        return;
    }

    const auto projectPageCount = static_cast<int>(m_openedSession->project.pages().size());

    if (s.selectedRegionIds.isEmpty()) {
        PageInspectorState pageState;
        pageState.pageNumber = page->orderIndex() + 1;
        pageState.pageCount = projectPageCount;
        pageState.imageSize = page->imageSize();
        pageState.readingDirection = m_openedSession->project.readingDirection();
        pageState.regionCount = static_cast<int>(page->regions().size());
        pageState.needsReviewCount = s.processing.needsReviewRegionIds.size();
        for (const auto& region : page->regions()) {
            if (!region.source().text.empty()) {
                ++pageState.ocrCompletedCount;
            }
            if (!region.translation().effectiveText().empty()) {
                ++pageState.translatedCount;
            }
        }
        s.inspector = pageState;
        return;
    }

    if (s.selectedRegionIds.size() == 1) {
        const auto* region = page->findRegion(s.selectedRegionIds.first());
        if (region == nullptr) {
            s.inspector = PageInspectorState{};
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
        s.inspector = single;
        return;
    }

    MultiRegionInspectorState multi;
    multi.pageId = page->id();
    multi.regionIds = s.selectedRegionIds;
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
            multi.horizontalAlignment = layout.alignment.horizontal;
            multi.verticalAlignment = layout.alignment.vertical;
            multi.orientation = layout.orientation;
            multi.textColorHex = textColorHex;
            multi.strokeColorHex = strokeColorHex;
            multi.isCleared = region->isCleared();
            first = false;
        } else {
            // A mismatch across the selection reports mixed (nullopt), never the first value.
            if (multi.fontFamily.has_value() && *multi.fontFamily != fontFamily) {
                multi.fontFamily.reset();
            }
            if (multi.fontWeight.has_value() && *multi.fontWeight != layout.fontWeight) {
                multi.fontWeight.reset();
            }
            if (multi.fontSizePt.has_value() && *multi.fontSizePt != layout.fontSizePt) {
                multi.fontSizePt.reset();
            }
            if (multi.horizontalAlignment.has_value() && *multi.horizontalAlignment != layout.alignment.horizontal) {
                multi.horizontalAlignment.reset();
            }
            if (multi.verticalAlignment.has_value() && *multi.verticalAlignment != layout.alignment.vertical) {
                multi.verticalAlignment.reset();
            }
            if (multi.orientation.has_value() && *multi.orientation != layout.orientation) {
                multi.orientation.reset();
            }
            if (multi.textColorHex.has_value() && *multi.textColorHex != textColorHex) {
                multi.textColorHex.reset();
            }
            if (multi.strokeColorHex.has_value() && *multi.strokeColorHex != strokeColorHex) {
                multi.strokeColorHex.reset();
            }
            if (multi.isCleared.has_value() && *multi.isCleared != region->isCleared()) {
                multi.isCleared.reset();
            }
        }
        multi.allLocked = multi.allLocked && region->isLocked();
    }
    if (first) {
        // No selected id resolved to a live region; fall back to the page summary.
        s.inspector = PageInspectorState{};
        return;
    }
    s.inspector = multi;
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

    auto context = m_operationRegistry->createOperation(m_state.projectSessionGeneration, m_state.viewGeneration);
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

    auto context = m_operationRegistry->createOperation(m_state.projectSessionGeneration, m_state.viewGeneration);

    ThumbnailTicket ticket;
    ticket.operationId = context.operationId;
    ticket.projectSessionGeneration = m_state.projectSessionGeneration;
    const QString key = QString::fromStdString(pageId.toString());
    m_thumbnailInFlight.insert(key, ticket);
    qInfo().noquote() << "[ThumbnailVM] start page=" << key
                      << "operation=" << QString::fromStdString(context.operationId.toString());

    application::LoadOrGenerateThumbnailRequest request;
    request.projectId = m_session.projectId;
    request.projectRoot = m_session.projectRootPath;
    request.pageId = pageId;
    request.originalAssetPath = page->originalAssetPath;
    request.originalSize = QSize(page->imageSize.width, page->imageSize.height);
    request.targetSize = QSize(256, 256); // Assuming this is standard thumbnail size

    auto* watcher = new QFutureWatcher<application::Result<QImage>>(this);
    m_thumbnailWatchers.insert(key, watcher);

    connect(watcher, &QFutureWatcher<application::Result<QImage>>::finished, this, [this, pageId]() {
        handleThumbnailFinished(pageId);
    });

    watcher->setFuture(m_thumbnailUseCase->execute(request, context));
}

void EditorViewModel::handleThumbnailFinished(const domain::PageId& pageId) {
    const QString key = QString::fromStdString(pageId.toString());

    if (!m_thumbnailInFlight.contains(key)) {
        return; // Already cancelled or handled
    }

    auto ticket = m_thumbnailInFlight.take(key);
    auto* watcher = m_thumbnailWatchers.take(key);

    if (watcher) {
        watcher->deleteLater();
    }

    if (ticket.projectSessionGeneration == m_state.projectSessionGeneration) {
        auto result = watcher ? watcher->result()
                              : std::unexpected(application::makeApplicationError(application::ErrorCategory::Internal,
                                                                                  QStringLiteral("Missing watcher"),
                                                                                  QStringLiteral("Missing watcher")));

        if (result.has_value()) {
            qInfo().noquote() << "[ThumbnailVM] success page=" << key << "size=" << result.value().size();
            Q_EMIT thumbnailReady(pageId, result.value());
        } else {
            const auto& error = result.error();
            qWarning().noquote() << "[ThumbnailVM] failed page=" << key
                                 << "category=" << static_cast<int>(error.category) << "code=" << error.code
                                 << "message=" << error.safeMessage;
            if (error.category != application::ErrorCategory::Cancelled) {
                Q_EMIT thumbnailFailed(pageId, error.safeMessage);
            }
        }
    } else {
        qInfo().noquote() << "[ThumbnailVM] stale result discarded page=" << key
                          << "ticketSessionGeneration=" << ticket.projectSessionGeneration
                          << "currentSessionGeneration=" << m_state.projectSessionGeneration;
    }

    pumpThumbnailQueue();
}

void EditorViewModel::pumpThumbnailQueue() {
    while (!m_thumbnailQueue.isEmpty() && m_thumbnailInFlight.size() < kMaxConcurrentThumbnails) {
        auto pageId = m_thumbnailQueue.takeFirst();
        startThumbnail(pageId);
    }
}

void EditorViewModel::cancelThumbnailRequests() {
    if (m_operationRegistry) {
        for (const auto& ticket : std::as_const(m_thumbnailInFlight)) {
            m_operationRegistry->cancelOperation(ticket.operationId);
        }
    }
    m_thumbnailInFlight.clear();
    m_thumbnailQueue.clear();

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

void EditorViewModel::createRegion(const domain::RectGeometry& geometry) {
    if (!m_hasSession || !m_undoStack || !m_commandContext || !m_state.currentPageId.isValid()) {
        return;
    }

    // Validate geometry: >= 8x8 inside page bounds.
    const auto pageSize = m_commandContext->pageSize(m_state.currentPageId);
    if (!geometry.isValid() || !geometry.isWithinBounds(pageSize)) {
        return;
    }
    if (geometry.width < canvas::kMinRegionDimension || geometry.height < canvas::kMinRegionDimension) {
        return;
    }

    const auto regionId = domain::RegionId::generate();
    const auto* page = m_openedSession->project.findPage(m_state.currentPageId);
    const int orderIndex = page ? static_cast<int>(page->regions().size()) : 0;

    sealActiveTextSession();
    auto* cmd =
        new command::CreateRegionCommand(m_commandContext.get(), m_state.currentPageId, regionId, geometry, orderIndex);
    m_undoStack->push(cmd);
    if (cmd->hasFaulted()) {
        m_undoHistoryFaulted = true;
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
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
    if (!m_hasSession || !m_undoStack || !m_commandContext || regionIds.isEmpty() || pageId != m_state.currentPageId) {
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
        return;
    }

    QVector<command::RegionPropertyCommand::PropertyEdit> edits;
    for (const auto& regionId : regionIds) {
        const auto* region = page->findRegion(regionId);
        // One missing or locked target vetoes the whole batch before anything is pushed.
        if (region == nullptr || region->isLocked()) {
            return;
        }
        auto layout = region->layout();
        if (!applyStyleFieldValue(layout, field, value) || !layout.isValid()) {
            return;
        }
        if (layout == region->layout()) {
            continue;
        }
        command::RegionPropertyCommand::PropertyEdit entry;
        entry.regionId = regionId;
        entry.before.regionId = regionId;
        entry.before.layout = region->layout();
        entry.after.regionId = regionId;
        entry.after.layout = layout;
        edits.append(std::move(entry));
    }
    if (edits.isEmpty()) {
        return;
    }

    sealActiveTextSession();
    auto* cmd = new command::RegionPropertyCommand(m_commandContext.get(), pageId, edits, QStringLiteral("Edit Style"));
    pushPropertyCommand(cmd);
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

    // push 前在同一 GUI event 再做最终 snapshot 校验
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

        // Wait, checking contentRevision is the best way to verify if it has changed since Prepare UseCase.
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
        // Is region valid? Wait, before state is enough?
        // Wait, if contentRevision didn't change, we are good.
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

    sealActiveTextSession();
    m_undoStack->undo();

    // The just-undone command sits at the stack index after undo.
    const auto* affected = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index()));
    const auto* affectedAdoption =
        dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index()));
    if (affected != nullptr && affected->hasFaulted()) {
        m_undoHistoryFaulted = true;
    } else if (affected != nullptr && affected->pageId().isValid() && affected->pageId() != m_state.currentPageId) {
        selectPage(affected->pageId());
    } else if (affectedAdoption != nullptr && affectedAdoption->hasFaulted()) {
        m_undoHistoryFaulted = true;
    } else if (affectedAdoption != nullptr && affectedAdoption->pageId().isValid() &&
               affectedAdoption->pageId() != m_state.currentPageId) {
        selectPage(affectedAdoption->pageId());
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
}

void EditorViewModel::redo() {
    if (!m_hasSession || !m_undoStack || m_undoHistoryFaulted) {
        return;
    }
    if (!m_undoStack->canRedo()) {
        return;
    }

    sealActiveTextSession();

    // Peek at the command that will be redone to get its page target.
    const auto* pendingCmd = dynamic_cast<const command::RegionCommand*>(m_undoStack->command(m_undoStack->index()));
    const auto* pendingAdoption =
        dynamic_cast<const command::ProcessingAdoptionCommand*>(m_undoStack->command(m_undoStack->index()));
    domain::PageId targetPage;
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
    } else if (targetPage.isValid() && targetPage != m_state.currentPageId) {
        selectPage(targetPage);
    }

    rebuildCurrentPageProjection();
    refreshUndoState();
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

    auto context = m_operationRegistry->createOperation(m_state.projectSessionGeneration, m_state.viewGeneration);
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
        if (m_operationRegistry && m_exportOperation.isValid()) {
            if (!response.failures.isEmpty()) {
                m_operationRegistry->markFinished(m_exportOperation, response.failures.constFirst().error);
            } else {
                m_operationRegistry->markFinished(m_exportOperation);
            }
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
    if (m_operationRegistry && m_exportOperation.isValid()) {
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

void EditorViewModel::updateProcessingState(const EditorProcessingState& processingState) {
    updateState([&](EditorState& s) { s.processing = processingState; });
}

} // namespace manga::ui::screen::editor
