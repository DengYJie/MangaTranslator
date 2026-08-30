#include "ui/screen/editor/EditorViewModel.h"

#include "application/mapping/ProjectMapper.h"
#include "application/ports/IPageImageLoader.h"
#include "application/ports/IThumbnailStore.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/thumbnail/ThumbnailUseCases.h"
#include "application/workflow/OperationRegistry.h"

#include <QFileInfo>

namespace manga::ui::screen::editor {

EditorViewModel::EditorViewModel(application::IThumbnailStore* thumbnailStore,
                                 application::IPageImageLoader* pageImageLoader,
                                 application::OperationRegistry* operationRegistry,
                                 QObject* parent)
    : base::BaseViewModel<EditorViewModel, EditorState>(parent), m_thumbnailStore(thumbnailStore),
      m_pageImageLoader(pageImageLoader), m_operationRegistry(operationRegistry) {
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
}

EditorViewModel::~EditorViewModel() {
    cancelThumbnailRequests();
    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
    }
}

void EditorViewModel::openProjectSession(application::OpenedProjectSession session) {
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
    invalidateRequests();

    m_session = application::mapping::ProjectMapper::toSessionDescriptor(session);
    m_session.warnings = session.warnings;
    m_openedSession = std::move(session);
    m_hasSession = true;
    m_viewTransforms.clear();

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
            entry.regionCount = 0;
            s.pages.append(entry);
        }

        s.projectSessionGeneration++;
        s.viewGeneration++;
        s.notice.reset();
        s.isPageLoading = false;
        s.currentPageId = domain::PageId{};
    });

    Q_EMIT thumbnailsInvalidated();

    if (!m_state.pages.isEmpty()) {
        selectPage(m_state.pages.first().id);
    }
}

void EditorViewModel::closeProjectSession() {
    cancelThumbnailRequests();
    if (m_operationRegistry && m_pageLoadOperation.isValid()) {
        m_operationRegistry->cancelOperation(m_pageLoadOperation);
        m_pageLoadOperation = application::OperationId{};
    }
    invalidateRequests();

    m_hasSession = false;
    m_session = application::ProjectSessionDescriptor{};
    m_openedSession.reset();
    m_viewTransforms.clear();

    const auto projGen = m_state.projectSessionGeneration + 1;
    const auto viewGen = m_state.viewGeneration + 1;
    updateState([&](EditorState& s) {
        s = EditorState{};
        s.projectSessionGeneration = projGen;
        s.viewGeneration = viewGen;
    });

    Q_EMIT thumbnailsInvalidated();
}

void EditorViewModel::selectPage(const domain::PageId& pageId) {
    if (!m_hasSession || pageId == m_state.currentPageId || findPage(pageId) == nullptr) {
        return;
    }

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
    });

    Q_EMIT thumbnailsInvalidated();

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
    const bool inQueue = m_thumbnailQueue.contains(pageId);

    if ((inFlight || inQueue) && !force) {
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
        ViewTransform transform;
        transform.zoom = std::clamp(zoom, 0.05, 16.0);
        transform.centerOnPage = centerOnPage;
        m_viewTransforms.insert(QString::fromStdString(m_state.currentPageId.toString()), transform);
    }
}

void EditorViewModel::dismissNotice() {
    if (m_state.notice.has_value()) {
        updateState([&](EditorState& s) { s.notice.reset(); });
    }
}

void EditorViewModel::startPageLoad(const domain::PageId& pageId) {
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
                EditorNotice notice;
                notice.safeMessage = error.safeMessage;
                notice.retryable = error.retryable || error.recovery == application::RecoveryAction::Retry;
                s.notice = notice;
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
    ticket.viewGeneration = m_state.viewGeneration;
    const QString key = QString::fromStdString(pageId.toString());
    m_thumbnailInFlight.insert(key, ticket);

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

    if (ticket.viewGeneration == m_state.viewGeneration) {
        auto result = watcher ? watcher->result()
                              : std::unexpected(application::makeApplicationError(application::ErrorCategory::Internal,
                                                                                  QStringLiteral("Missing watcher"),
                                                                                  QStringLiteral("Missing watcher")));

        if (result.has_value()) {
            Q_EMIT thumbnailReady(pageId, result.value());
        } else {
            const auto& error = result.error();
            if (error.category != application::ErrorCategory::Cancelled) {
                Q_EMIT thumbnailFailed(pageId, error.safeMessage);
            }
        }
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

} // namespace manga::ui::screen::editor
