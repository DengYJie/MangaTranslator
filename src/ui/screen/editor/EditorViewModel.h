#pragma once

#include "application/dto/ImportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/operation/AsyncResult.h"
#include "ui/base/BaseViewModel.h"
#include "ui/screen/editor/EditorState.h"

#include <QFutureWatcher>
#include <QHash>
#include <QSet>

namespace manga::application {
class IPageImageLoader;
class IThumbnailStore;
class LoadOrGenerateThumbnailUseCase;
class LoadPageImageUseCase;
class OperationRegistry;
} // namespace manga::application

namespace manga::ui::screen::editor {

/// Drives the editor skeleton: project session, current page image loading and the
/// thumbnail pipeline (max 4 concurrent, generation-checked late results).
class EditorViewModel : public base::BaseViewModel<EditorViewModel, EditorState> {
    Q_OBJECT
public:
    static constexpr int kMaxConcurrentThumbnails = 4;

    EditorViewModel(application::IThumbnailStore* thumbnailStore,
                    application::IPageImageLoader* pageImageLoader,
                    application::OperationRegistry* operationRegistry,
                    QObject* parent = nullptr);
    ~EditorViewModel() override;

    void openProjectSession(const application::ProjectSessionDescriptor& session);
    void closeProjectSession();
    void selectPage(const domain::PageId& pageId);
    void selectAdjacentPage(int offset);
    void retryPageLoad();
    void requestThumbnail(const domain::PageId& pageId, bool force);
    /// Records the live canvas transform for the current page. Memory-only, no State churn.
    void noteViewTransform(double zoom, const QPointF& centerOnPage);
    void dismissNotice();

Q_SIGNALS:
    void stateChanged(const EditorState& state);
    void pageImageReady(const PageImageDelivery& delivery);
    void thumbnailReady(const domain::PageId& pageId, const QImage& image);
    void thumbnailFailed(const domain::PageId& pageId, const QString& safeMessage);
    /// viewGeneration bumped: in-flight thumbnail binds are discarded, rows must re-request.
    void thumbnailsInvalidated();

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
    [[nodiscard]] const application::PageDescriptor* findPage(const domain::PageId& pageId) const;

    application::IThumbnailStore* m_thumbnailStore;      // non-owning, may be null in tests
    application::IPageImageLoader* m_pageImageLoader;    // non-owning, may be null in tests
    application::OperationRegistry* m_operationRegistry; // non-owning
    // Member lifetime is required: LoadOrGenerateThumbnailUseCase continuations capture `this`.
    std::unique_ptr<application::LoadOrGenerateThumbnailUseCase> m_thumbnailUseCase;
    std::unique_ptr<application::LoadPageImageUseCase> m_pageLoadUseCase;

    application::ProjectSessionDescriptor m_session;
    bool m_hasSession{false};
    QHash<QString, ViewTransform> m_viewTransforms; // key: PageId string

    QFutureWatcher<application::Result<QImage>> m_pageLoadWatcher;
    quint64 m_pageLoadToken{0};
    application::OperationId m_pageLoadOperation{};

    QHash<QString, ThumbnailTicket> m_thumbnailInFlight; // key: PageId string
    QVector<domain::PageId> m_thumbnailQueue;
    QHash<QString, QFutureWatcher<application::Result<QImage>>*> m_thumbnailWatchers;
};

} // namespace manga::ui::screen::editor
