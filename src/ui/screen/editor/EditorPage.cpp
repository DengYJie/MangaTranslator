#include "ui/screen/editor/EditorPage.h"

#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace manga::ui::screen::editor {

EditorPage::EditorPage(EditorViewModel* viewModel, QWidget* parent) : base::BasePage(parent), m_viewModel(viewModel) {
    buildUi();

    connect(m_pageList, &widget::PageThumbnailList::pageActivated, m_viewModel, &EditorViewModel::selectPage);
    connect(m_pageList,
            &widget::PageThumbnailList::thumbnailRequested,
            m_viewModel,
            [this](const domain::PageId& pageId, bool force) { m_viewModel->requestThumbnail(pageId, force); });

    connect(m_viewModel, &EditorViewModel::thumbnailReady, m_pageList, &widget::PageThumbnailList::insertThumbnail);
    connect(
        m_viewModel, &EditorViewModel::thumbnailFailed, m_pageList, &widget::PageThumbnailList::markThumbnailFailed);
    connect(m_viewModel,
            &EditorViewModel::thumbnailsInvalidated,
            m_pageList,
            &widget::PageThumbnailList::resetLoadingThumbnails);

    connect(m_canvas, &ui::manga::MangaCanvas::viewTransformChanged, m_viewModel, &EditorViewModel::noteViewTransform);
    connect(m_canvas, &ui::manga::MangaCanvas::placeholderActionActivated, this, &EditorPage::handlePlaceholderAction);

    connect(m_viewModel, &EditorViewModel::pageImageReady, this, &EditorPage::handlePageImageReady);

    m_viewModel->observe(this, [this](const EditorState& state) { render(state); });

    auto* pageUpShortcut = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    pageUpShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pageUpShortcut, &QShortcut::activated, m_viewModel, [this]() { m_viewModel->selectAdjacentPage(-1); });

    auto* pageDownShortcut = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    pageDownShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pageDownShortcut, &QShortcut::activated, m_viewModel, [this]() { m_viewModel->selectAdjacentPage(1); });

    setDefaultFocusWidget(m_canvas);
}

void EditorPage::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(m_splitter);

    m_pageList = new widget::PageThumbnailList(m_splitter);
    m_splitter->addWidget(m_pageList);

    m_canvas = new ui::manga::MangaCanvas(m_splitter);
    m_splitter->addWidget(m_canvas);

    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);

    // Initial width 232 px for page list
    m_splitter->setSizes({232, 10000});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
}

void EditorPage::render(const EditorState& state) {
    // convert and set pages
    QVector<widget::PageListEntry> entries;
    entries.reserve(state.pages.size());
    for (const auto& p : state.pages) {
        entries.append({p.id, p.orderIndex, p.displayName, p.aspectRatio()});
    }
    m_pageList->model()->setPages(entries); // Model 自带相等短路

    m_pageList->setCurrentPageId(state.currentPageId);

    if (!state.projectId.isValid()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoProject;
        m_canvas->setContentState(m_lastPlaceholder, tr("打开或创建项目以开始编辑"), tr("打开项目"));
    } else if (state.pages.isEmpty()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoPage;
        m_canvas->setContentState(m_lastPlaceholder, tr("项目中还没有页面"), tr("导入页面"));
    } else if (state.notice && state.notice->retryable) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::Error;
        m_canvas->setContentState(m_lastPlaceholder, state.notice->safeMessage, tr("重试加载"));
    } else if (state.isPageLoading) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::Loading;
        QString displayName;
        for (const auto& p : state.pages) {
            if (p.id == state.currentPageId) {
                displayName = p.displayName.isEmpty() ? QString::fromStdString(p.id.toString()) : p.displayName;
                break;
            }
        }
        m_canvas->setContentState(m_lastPlaceholder, displayName, QString());
    }
    // Image state is not transitioned here, it is driven by pageImageReady signal.
}

void EditorPage::handlePageImageReady(const PageImageDelivery& delivery) {
    m_canvas->setPageImage(delivery.image);
    if (delivery.hasSavedTransform) {
        m_canvas->setZoomFactor(delivery.zoom);
        m_canvas->centerOnPage(delivery.centerOnPage);
    } else {
        m_canvas->fitToPage();
    }
}

void EditorPage::handlePlaceholderAction() {
    switch (m_lastPlaceholder) {
        case ui::manga::MangaCanvas::ContentState::NoProject:
            Q_EMIT openProjectRequested();
            break;
        case ui::manga::MangaCanvas::ContentState::NoPage:
            Q_EMIT importPagesRequested();
            break;
        case ui::manga::MangaCanvas::ContentState::Error:
            m_viewModel->retryPageLoad();
            break;
        default:
            break;
    }
}

} // namespace manga::ui::screen::editor
