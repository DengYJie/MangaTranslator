#include "ui/screen/editor/EditorPage.h"

#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/widget/EditorCommandBar.h"
#include "ui/widget/InspectorPanel.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace manga::ui::screen::editor {

namespace {
constexpr int kBreakpointWide = 1440;
constexpr int kBreakpointStandard = 1200;
constexpr int kBreakpointCompact = 1008;
constexpr int kBreakpointNarrow = 720;
constexpr int kFlatHeightBreakpoint = 560;

constexpr int kPagePaneWideWidth = 232;
constexpr int kPagePaneStandardWidth = 220;
constexpr int kPagePaneCompactWidth = 96;

constexpr int kInspectorWideWidth = 352;
constexpr int kInspectorStandardWidth = 328;
constexpr int kInspectorCompactWidth = 320;
} // namespace

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

    connect(m_canvas,
            &ui::manga::MangaCanvas::regionCreateCommitted,
            m_viewModel,
            [this](const domain::RectGeometry& geometry) { m_viewModel->createRegion(geometry); });
    connect(m_canvas,
            &ui::manga::MangaCanvas::regionSelectionRequested,
            m_viewModel,
            [this](const domain::RegionId& regionId, ui::manga::CanvasSelectionIntent intent) {
                RegionSelectionMode mode = RegionSelectionMode::Replace;
                switch (intent) {
                    case ui::manga::CanvasSelectionIntent::Replace:
                        mode = RegionSelectionMode::Replace;
                        break;
                    case ui::manga::CanvasSelectionIntent::Toggle:
                        mode = RegionSelectionMode::Toggle;
                        break;
                    case ui::manga::CanvasSelectionIntent::Range:
                        mode = RegionSelectionMode::Range;
                        break;
                }
                m_viewModel->selectRegion(regionId, mode);
            });
    connect(m_canvas,
            &ui::manga::MangaCanvas::regionGeometryCommitted,
            m_viewModel,
            [this](const domain::RegionId& regionId,
                   const domain::RectGeometry& geometry,
                   ui::manga::CanvasEditKind kind) { m_viewModel->commitRegionGeometry(regionId, geometry, kind); });
    connect(m_canvas,
            &ui::manga::MangaCanvas::regionDeleteRequested,
            m_viewModel,
            [this](const domain::RegionId& regionId) { m_viewModel->deleteRegions({regionId}); });

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
    connect(m_inspector,
            &widget::InspectorPanel::ocrRequested,
            this,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds) {
                if (auto* ctrl = m_viewModel->processingController()) {
                    for (const auto& rId : regionIds) {
                        ctrl->recognizeRegion(pageId, rId);
                    }
                }
            });
    connect(m_inspector,
            &widget::InspectorPanel::translateRequested,
            this,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& /*regionIds*/) {
                if (auto* ctrl = m_viewModel->processingController()) {
                    ctrl->smartTranslatePage(pageId);
                }
            });
    connect(m_inspector,
            &widget::InspectorPanel::clearRequested,
            this,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds) {
                if (auto* ctrl = m_viewModel->processingController()) {
                    for (const auto& rId : regionIds) {
                        ctrl->clearRegion(pageId, rId);
                    }
                }
            });
    connect(m_inspector,
            &widget::InspectorPanel::layoutRequested,
            this,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds) {
                if (auto* ctrl = m_viewModel->processingController()) {
                    for (const auto& rId : regionIds) {
                        ctrl->layoutRegion(pageId, rId);
                    }
                }
            });

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
    connect(
        m_commandBar, &widget::EditorCommandBar::commitRequested, m_viewModel, &EditorViewModel::sealTextEditSession);

    connect(m_commandBar, &widget::EditorCommandBar::pagePaneToggleRequested, this, &EditorPage::togglePagePane);
    connect(
        m_commandBar, &widget::EditorCommandBar::inspectorPaneToggleRequested, this, &EditorPage::toggleInspectorPane);

    m_viewModel->observe(this, [this](const EditorState& state) { render(state); });

    auto* pageUpShortcut = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    pageUpShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pageUpShortcut, &QShortcut::activated, m_viewModel, [this]() { m_viewModel->selectAdjacentPage(-1); });

    auto* pageDownShortcut = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    pageDownShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pageDownShortcut, &QShortcut::activated, m_viewModel, [this]() { m_viewModel->selectAdjacentPage(1); });

    auto* f6Shortcut = new QShortcut(QKeySequence(Qt::Key_F6), this);
    f6Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f6Shortcut, &QShortcut::activated, this, [this]() { cyclePaneFocus(true); });

    auto* shiftF6Shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6), this);
    shiftF6Shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shiftF6Shortcut, &QShortcut::activated, this, [this]() { cyclePaneFocus(false); });

    setDefaultFocusWidget(m_canvas);
}

void EditorPage::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

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
    m_pagePaneCard = pagePaneCard;

    auto* canvasPaneCard = new fluent::layout::Card(m_splitter);
    canvasPaneCard->setObjectName(QStringLiteral("editorCanvasPaneCard"));
    canvasPaneCard->setAppearance(fluent::layout::Card::Layer);
    auto* canvasPaneLayout = new QVBoxLayout(canvasPaneCard);
    canvasPaneLayout->setContentsMargins(0, 0, 0, 0);
    canvasPaneLayout->setSpacing(0);

    m_commandBar = new widget::EditorCommandBar(m_viewModel, canvasPaneCard);
    m_commandBar->setObjectName(QStringLiteral("editorCommandBar"));
    canvasPaneLayout->addWidget(m_commandBar);

    m_canvas = new ui::manga::MangaCanvas(canvasPaneCard);
    canvasPaneLayout->addWidget(m_canvas, 1);
    m_splitter->addWidget(canvasPaneCard);

    auto* inspectorCard = new fluent::layout::Card(m_splitter);
    inspectorCard->setObjectName(QStringLiteral("editorInspectorCard"));
    inspectorCard->setAppearance(fluent::layout::Card::Layer);
    auto* inspectorLayout = new QVBoxLayout(inspectorCard);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    m_inspector = new widget::InspectorPanel(inspectorCard);
    inspectorLayout->addWidget(m_inspector);
    m_splitter->addWidget(inspectorCard);
    m_inspectorCard = inspectorCard;

    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setCollapsible(2, true);

    m_splitter->setSizes({kPagePaneWideWidth, 10000, kInspectorWideWidth});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
}

void EditorPage::updateResponsiveLayout(int availableWidth) {
    base::BasePage::updateResponsiveLayout(availableWidth);
    m_lastAvailableWidth = availableWidth;

    if (availableWidth >= kBreakpointWide) {
        m_breakpointMode = BreakpointMode::Wide;
    } else if (availableWidth >= kBreakpointStandard) {
        m_breakpointMode = BreakpointMode::Standard;
    } else if (availableWidth >= kBreakpointCompact) {
        m_breakpointMode = BreakpointMode::Compact;
    } else if (availableWidth >= kBreakpointNarrow) {
        m_breakpointMode = BreakpointMode::Narrow;
    } else {
        m_breakpointMode = BreakpointMode::Minimum;
    }

    applySplitterLayout();
}

void EditorPage::applySplitterLayout() {
    if (!m_splitter || !m_pagePaneCard || !m_inspectorCard) {
        return;
    }

    switch (m_breakpointMode) {
        case BreakpointMode::Wide:
            m_pagePaneCard->setVisible(true);
            m_inspectorCard->setVisible(true);
            m_splitter->setSizes({kPagePaneWideWidth,
                                  qMax(480, m_lastAvailableWidth - kPagePaneWideWidth - kInspectorWideWidth - 16),
                                  kInspectorWideWidth});
            break;
        case BreakpointMode::Standard:
            m_pagePaneCard->setVisible(true);
            m_inspectorCard->setVisible(true);
            m_splitter->setSizes(
                {kPagePaneStandardWidth,
                 qMax(480, m_lastAvailableWidth - kPagePaneStandardWidth - kInspectorStandardWidth - 16),
                 kInspectorStandardWidth});
            break;
        case BreakpointMode::Compact:
            m_pagePaneCard->setVisible(true);
            m_inspectorCard->setVisible(true);
            m_splitter->setSizes({kPagePaneCompactWidth,
                                  qMax(480, m_lastAvailableWidth - kPagePaneCompactWidth - kInspectorCompactWidth - 16),
                                  kInspectorCompactWidth});
            break;
        case BreakpointMode::Narrow:
        case BreakpointMode::Minimum:
            if (m_pageDrawerOpen) {
                m_pagePaneCard->setVisible(true);
                m_inspectorCard->setVisible(false);
                const int drawerWidth =
                    (m_breakpointMode == BreakpointMode::Minimum) ? qMin(m_lastAvailableWidth - 40, 232) : 232;
                m_splitter->setSizes({drawerWidth, qMax(100, m_lastAvailableWidth - drawerWidth - 8), 0});
            } else if (m_inspectorDrawerOpen) {
                m_pagePaneCard->setVisible(false);
                m_inspectorCard->setVisible(true);
                const int drawerWidth =
                    (m_breakpointMode == BreakpointMode::Minimum) ? qMin(m_lastAvailableWidth - 40, 320) : 320;
                m_splitter->setSizes({0, qMax(100, m_lastAvailableWidth - drawerWidth - 8), drawerWidth});
            } else {
                m_pagePaneCard->setVisible(false);
                m_inspectorCard->setVisible(false);
                m_splitter->setSizes({0, m_lastAvailableWidth, 0});
            }
            break;
    }
}

void EditorPage::setPageDrawerVisible(bool visible) {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        m_pageDrawerOpen = visible;
        if (visible) {
            m_inspectorDrawerOpen = false;
        }
        applySplitterLayout();
    } else {
        m_pagePaneCard->setVisible(visible);
    }
}

void EditorPage::setInspectorDrawerVisible(bool visible) {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        m_inspectorDrawerOpen = visible;
        if (visible) {
            m_pageDrawerOpen = false;
        }
        applySplitterLayout();
    } else {
        m_inspectorCard->setVisible(visible);
    }
}

void EditorPage::togglePagePane() {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        setPageDrawerVisible(!m_pageDrawerOpen);
    } else {
        m_pagePaneCard->setVisible(!m_pagePaneCard->isVisible());
    }
}

void EditorPage::toggleInspectorPane() {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        setInspectorDrawerVisible(!m_inspectorDrawerOpen);
    } else {
        m_inspectorCard->setVisible(!m_inspectorCard->isVisible());
    }
}

bool EditorPage::isPageDrawerVisible() const {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        return m_pageDrawerOpen && m_pagePaneCard->isVisible();
    }
    return m_pagePaneCard->isVisible();
}

bool EditorPage::isInspectorDrawerVisible() const {
    if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
        return m_inspectorDrawerOpen && m_inspectorCard->isVisible();
    }
    return m_inspectorCard->isVisible();
}

void EditorPage::cyclePaneFocus(bool forward) {
    QWidget* focusW = QApplication::focusWidget();
    enum class Pane {
        PageList,
        Canvas,
        Inspector,
        CommandBar
    };
    Pane current = Pane::Canvas;

    if (m_pageList && (m_pageList->hasFocus() || m_pageList->isAncestorOf(focusW))) {
        current = Pane::PageList;
    } else if (m_canvas && (m_canvas->hasFocus() || m_canvas->isAncestorOf(focusW))) {
        current = Pane::Canvas;
    } else if (m_inspector && (m_inspector->hasFocus() || m_inspector->isAncestorOf(focusW))) {
        current = Pane::Inspector;
    } else if (m_commandBar && (m_commandBar->hasFocus() || m_commandBar->isAncestorOf(focusW))) {
        current = Pane::CommandBar;
    }

    Pane next = Pane::Canvas;
    if (forward) {
        switch (current) {
            case Pane::PageList:
                next = Pane::Canvas;
                break;
            case Pane::Canvas:
                next = (m_inspectorCard && m_inspectorCard->isVisible()) ? Pane::Inspector : Pane::CommandBar;
                break;
            case Pane::Inspector:
                next = Pane::CommandBar;
                break;
            case Pane::CommandBar:
                next = (m_pagePaneCard && m_pagePaneCard->isVisible()) ? Pane::PageList : Pane::Canvas;
                break;
        }
    } else {
        switch (current) {
            case Pane::PageList:
                next = Pane::CommandBar;
                break;
            case Pane::CommandBar:
                next = (m_inspectorCard && m_inspectorCard->isVisible()) ? Pane::Inspector : Pane::Canvas;
                break;
            case Pane::Inspector:
                next = Pane::Canvas;
                break;
            case Pane::Canvas:
                next = (m_pagePaneCard && m_pagePaneCard->isVisible()) ? Pane::PageList : Pane::CommandBar;
                break;
        }
    }

    switch (next) {
        case Pane::PageList:
            if (m_pageList)
                m_pageList->setFocus();
            break;
        case Pane::Canvas:
            if (m_canvas)
                m_canvas->setFocus();
            break;
        case Pane::Inspector:
            if (m_inspector)
                m_inspector->setFocus();
            break;
        case Pane::CommandBar:
            if (m_commandBar)
                m_commandBar->setFocus();
            break;
    }
}

void EditorPage::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_breakpointMode == BreakpointMode::Narrow || m_breakpointMode == BreakpointMode::Minimum) {
            if (m_pageDrawerOpen || m_inspectorDrawerOpen) {
                m_pageDrawerOpen = false;
                m_inspectorDrawerOpen = false;
                applySplitterLayout();
                event->accept();
                return;
            }
        }
    } else if (event->key() == Qt::Key_F6) {
        cyclePaneFocus(!(event->modifiers() & Qt::ShiftModifier));
        event->accept();
        return;
    }
    base::BasePage::keyPressEvent(event);
}

void EditorPage::resizeEvent(QResizeEvent* event) {
    base::BasePage::resizeEvent(event);
    const bool flatHeight = height() < kFlatHeightBreakpoint;
    if (layout()) {
        const int m = flatHeight ? 2 : 8;
        const int s = flatHeight ? 2 : 4;
        layout()->setContentsMargins(m, m, m, m);
        layout()->setSpacing(s);
    }
}

void EditorPage::render(const EditorState& state) {
    QVector<widget::PageListEntry> entries;
    entries.reserve(state.pages.size());
    for (const auto& p : state.pages) {
        entries.append({p.id, p.orderIndex, p.displayName, p.aspectRatio()});
    }
    m_pageList->model()->setPages(entries);

    m_pageList->setCurrentPageId(state.currentPageId);
    if (state.currentPageId.isValid()) {
        m_pageList->setPageProcessingStatus(state.currentPageId, static_cast<int>(state.processing.status));
    }

    m_inspector->setInspectorState(state.inspector);

    // Sync canvas tool and region projections from the authoritative State.
    ui::manga::CanvasTool canvasTool = ui::manga::CanvasTool::Select;
    switch (state.activeTool) {
        case CanvasTool::Select:
            canvasTool = ui::manga::CanvasTool::Select;
            break;
        case CanvasTool::Pan:
            canvasTool = ui::manga::CanvasTool::Pan;
            break;
        case CanvasTool::CreateRegion:
            canvasTool = ui::manga::CanvasTool::CreateRegion;
            break;
    }
    m_canvas->setTool(canvasTool);

    QVector<ui::manga::CanvasRegionDisplay> displayRegions;
    displayRegions.reserve(state.regions.size());
    for (const auto& rp : state.regions) {
        ui::manga::CanvasRegionDisplay display;
        display.id = rp.id;
        display.geometry = rp.geometry;
        display.selected = state.selectedRegionIds.contains(rp.id);
        displayRegions.append(display);
    }
    m_canvas->setRegions(displayRegions);

    for (const auto& p : state.pages) {
        if (p.id == state.currentPageId) {
            m_canvas->setPageBounds(p.imageSize);
            break;
        }
    }

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
