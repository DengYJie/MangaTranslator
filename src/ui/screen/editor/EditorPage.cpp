#include "ui/screen/editor/EditorPage.h"

#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/widget/EditorCommandBar.h"
#include "ui/widget/InspectorPanel.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/FluentQt.h>
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

    connect(m_inspector,
            &widget::InspectorPanel::deleteRegionsRequested,
            m_viewModel,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds) {
                if (pageId == m_viewModel->state().currentPageId) {
                    m_viewModel->deleteRegions(regionIds);
                }
            });
    connect(m_inspector,
            &widget::InspectorPanel::fieldEditCommitted,
            m_viewModel,
            &EditorViewModel::commitInspectorFieldEdit);
    connect(m_inspector,
            &widget::InspectorPanel::candidateAdoptionRequested,
            m_viewModel,
            &EditorViewModel::adoptCandidate);
    connect(m_inspector,
            &widget::InspectorPanel::textEditingSessionFinished,
            m_viewModel,
            &EditorViewModel::sealTextEditSession);

    connect(m_commandBar, &widget::EditorCommandBar::zoomInRequested, m_canvas, &ui::manga::MangaCanvas::zoomIn);
    connect(m_commandBar, &widget::EditorCommandBar::zoomOutRequested, m_canvas, &ui::manga::MangaCanvas::zoomOut);
    connect(m_commandBar, &widget::EditorCommandBar::fitToPageRequested, m_canvas, &ui::manga::MangaCanvas::fitToPage);
    connect(m_commandBar, &widget::EditorCommandBar::actualSizeRequested, m_canvas, [this]() {
        m_canvas->setZoomFactor(1.0);
    });
    connect(m_commandBar, &widget::EditorCommandBar::deleteSelectedRegionsRequested, this, [this]() {
        const auto& selected = m_viewModel->state().selectedRegionIds;
        if (!selected.isEmpty()) {
            m_viewModel->deleteRegions(selected);
        }
    });

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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    m_commandBar = new widget::EditorCommandBar(m_viewModel, this);
    m_commandBar->setObjectName(QStringLiteral("editorCommandBar"));
    layout->addWidget(m_commandBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(8);
    layout->addWidget(m_splitter);

    auto* pagePaneCard = new fluent::layout::Card(m_splitter);
    pagePaneCard->setObjectName(QStringLiteral("editorPagePaneCard"));
    pagePaneCard->setAppearance(fluent::layout::Card::Layer);
    auto* pagePaneLayout = new QVBoxLayout(pagePaneCard);
    pagePaneLayout->setContentsMargins(0, 0, 0, 0);
    pagePaneLayout->setSpacing(0);
    m_pageList = new widget::PageThumbnailList(pagePaneCard);
    pagePaneLayout->addWidget(m_pageList);
    m_splitter->addWidget(pagePaneCard);

    m_canvas = new ui::manga::MangaCanvas(m_splitter);
    m_splitter->addWidget(m_canvas);

    auto* inspectorCard = new fluent::layout::Card(m_splitter);
    inspectorCard->setObjectName(QStringLiteral("editorInspectorCard"));
    inspectorCard->setAppearance(fluent::layout::Card::Layer);
    auto* inspectorLayout = new QVBoxLayout(inspectorCard);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    m_inspector = new widget::InspectorPanel(inspectorCard);
    inspectorLayout->addWidget(m_inspector);
    m_splitter->addWidget(inspectorCard);

    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
    m_splitter->setCollapsible(2, false);

    m_splitter->setSizes({232, 10000, 352});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
}

void EditorPage::render(const EditorState& state) {
    QVector<widget::PageListEntry> entries;
    entries.reserve(state.pages.size());
    for (const auto& p : state.pages) {
        entries.append({p.id, p.orderIndex, p.displayName, p.aspectRatio()});
    }
    m_pageList->model()->setPages(entries);

    m_pageList->setCurrentPageId(state.currentPageId);

    m_inspector->setInspectorState(state.inspector);

    if (!state.projectId.isValid()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoProject;
        m_canvas->setContentState(m_lastPlaceholder, tr("打开或创建项目以开始编辑"), tr("打开项目"));
    } else if (state.pages.isEmpty()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoPage;
        m_canvas->setContentState(m_lastPlaceholder, tr("项目中还没有页面"), tr("导入页面"));
    } else if (state.notice) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::Error;
        m_canvas->setContentState(
            m_lastPlaceholder, state.notice->safeMessage, state.notice->retryable ? tr("重试加载") : QString());
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
