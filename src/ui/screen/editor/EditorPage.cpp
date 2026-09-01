#include "ui/screen/editor/EditorPage.h"

#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/InspectorState.h"
#include "ui/widget/EditorCommandBar.h"
#include "ui/widget/InspectorPanel.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"
#include "ui/widget/PaneCard.h"
#include "ui/widget/PaneSplitter.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/FluentQt.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShortcut>
#include <QVBoxLayout>

namespace manga::ui::screen::editor {

namespace {
constexpr int kBreakpointWide = 1440;
constexpr int kBreakpointStandard = 1200;
constexpr int kBreakpointCompact = 1008;
constexpr int kBreakpointNarrow = 720;

constexpr int kPagePaneWideWidth = 232;
constexpr int kPagePaneStandardWidth = 220;
constexpr int kPagePaneCompactWidth = 96;
constexpr int kPagePaneMaxWidth = 248;

constexpr int kInspectorWideWidth = 352;
constexpr int kInspectorStandardWidth = 328;
constexpr int kInspectorCompactWidth = 320;
constexpr int kInspectorMaxWidth = 380;
} // namespace

QSplitter* EditorPage::splitterForTest() const {
    return m_splitter;
}

void EditorPage::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    // Fill the page with the same layer surface the command bar sits on, so
    // the splitter handles' transparent strips read as one continuous pane
    // background instead of a contrasting gap.
    QPainter painter(this);
    painter.fillRect(rect(), themeColorsRef().bgLayer);
}

EditorPage::EditorPage(EditorViewModel* viewModel, QWidget* parent) : base::BasePage(parent), m_viewModel(viewModel) {
    buildUi();

    connect(m_pageList, &widget::PageThumbnailList::pageActivated, m_viewModel, &EditorViewModel::selectPage);
    connect(m_pageList, &widget::PageThumbnailList::pagesSelected, m_viewModel, &EditorViewModel::setSelectedPages);
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
    connect(
        m_canvas,
        &ui::manga::MangaCanvas::regionsGeometryCommitted,
        m_viewModel,
        [this](const QVector<domain::RegionId>& regionIds,
               const QVector<domain::RectGeometry>& geometries,
               ui::manga::CanvasEditKind kind) { m_viewModel->commitRegionGeometries(regionIds, geometries, kind); });
    connect(m_canvas,
            &ui::manga::MangaCanvas::regionDeleteRequested,
            m_viewModel,
            [this](const domain::RegionId& regionId) { m_viewModel->deleteRegions({regionId}); });
    connect(m_canvas, &ui::manga::MangaCanvas::regionTextEditRequested, this, [this](const domain::RegionId& regionId) {
        m_viewModel->selectRegion(regionId, RegionSelectionMode::Replace);
        QMetaObject::invokeMethod(
            m_inspector, [this]() { m_inspector->focusTranslationEditor(); }, Qt::QueuedConnection);
    });
    connect(m_canvas,
            &ui::manga::MangaCanvas::regionContextActionRequested,
            this,
            [this](const domain::RegionId& regionId, const QString& action) {
                m_viewModel->selectRegion(regionId, RegionSelectionMode::Replace);
                const auto pageId = m_viewModel->state().currentPageId;
                if (auto* ctrl = m_viewModel->processingController()) {
                    if (action == QStringLiteral("ocr")) {
                        ctrl->recognizeRegion(pageId, regionId);
                    } else if (action == QStringLiteral("translate")) {
                        ctrl->smartTranslateRegions(pageId, {regionId});
                    }
                }
                if (action == QStringLiteral("delete")) {
                    m_viewModel->deleteRegions({regionId});
                }
            });

    connect(m_viewModel, &EditorViewModel::pageImageReady, this, &EditorPage::handlePageImageReady);
    connect(m_viewModel, &EditorViewModel::previewImageReady, this, [this](const PageImageDelivery& delivery) {
        m_canvas->setTranslatedPreview(delivery.image);
    });

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
                qInfo().noquote() << "[OCRUI] request page=" << QString::fromStdString(pageId.toString())
                                  << "regions=" << regionIds.size()
                                  << "controller=" << (m_viewModel->processingController() != nullptr);
                if (auto* ctrl = m_viewModel->processingController()) {
                    for (const auto& rId : regionIds) {
                        ctrl->recognizeRegion(pageId, rId);
                    }
                }
            });
    connect(m_inspector,
            &widget::InspectorPanel::translateRequested,
            this,
            [this](const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds) {
                if (auto* ctrl = m_viewModel->processingController()) {
                    if (regionIds.isEmpty()) {
                        ctrl->smartTranslatePage(pageId);
                    } else {
                        ctrl->smartTranslateRegions(pageId, regionIds);
                    }
                }
            });
    connect(m_inspector, &widget::InspectorPanel::translationSettingsRequested, this, [this]() {
        Q_EMIT settingsRequested(QStringLiteral("settings/ai-translation"));
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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new widget::PaneSplitter(Qt::Horizontal, this);
    layout->addWidget(m_splitter);

    auto* pagePaneCard = new widget::PaneCard(m_splitter);
    pagePaneCard->setObjectName(QStringLiteral("editorPagePaneCard"));
    pagePaneCard->setAppearance(fluent::layout::Card::Layer);
    pagePaneCard->setMaximumWidth(kPagePaneMaxWidth);
    auto* pagePaneLayout = new QVBoxLayout(pagePaneCard);
    pagePaneLayout->setContentsMargins(0, 0, 0, 0);
    pagePaneLayout->setSpacing(0);
    m_pageList = new widget::PageThumbnailList(pagePaneCard);
    pagePaneLayout->addWidget(m_pageList);
    m_splitter->addWidget(pagePaneCard);
    m_pagePaneCard = pagePaneCard;

    auto* canvasPaneCard = new widget::PaneCard(m_splitter);
    canvasPaneCard->setObjectName(QStringLiteral("editorCanvasPaneCard"));
    canvasPaneCard->setAppearance(fluent::layout::Card::Canvas);
    auto* canvasPaneLayout = new QVBoxLayout(canvasPaneCard);
    canvasPaneLayout->setContentsMargins(0, 0, 0, 0);
    canvasPaneLayout->setSpacing(0);

    m_commandBar = new widget::EditorCommandBar(m_viewModel, canvasPaneCard);
    m_commandBar->setObjectName(QStringLiteral("editorCommandBar"));
    canvasPaneLayout->addWidget(m_commandBar);

    auto* canvasHost = new QWidget(canvasPaneCard);
    canvasHost->setObjectName(QStringLiteral("editorCanvasHost"));
    auto* canvasHostLayout = new QGridLayout(canvasHost);
    canvasHostLayout->setContentsMargins(0, 0, 0, 0);
    canvasHostLayout->setSpacing(0);

    m_canvas = new ui::manga::MangaCanvas(canvasHost);
    canvasHostLayout->addWidget(m_canvas, 0, 0);

    auto* toolRail = new fluent::layout::Card(canvasHost);
    m_canvasToolRail = toolRail;
    toolRail->setObjectName(QStringLiteral("editorCanvasToolRail"));
    toolRail->setAppearance(fluent::layout::Card::Layer);
    toolRail->raise();
    auto* toolLayout = new QVBoxLayout(toolRail);
    toolLayout->setContentsMargins(8, 8, 8, 8);
    toolLayout->setSpacing(6);
    toolLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* toolGroup = new QButtonGroup(toolRail);
    toolGroup->setExclusive(true);
    const auto addCanvasTool = [this, toolLayout, toolGroup](const QString& objectName,
                                                             const QString& glyph,
                                                             const QString& label,
                                                             CanvasTool tool) {
        auto* button = new fluent::basicinput::Button(toolLayout->parentWidget());
        button->setObjectName(objectName);
        button->setIconGlyph(glyph, Typography::IconSize::Standard);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setFluentStyle(fluent::basicinput::Button::Subtle);
        button->setFluentLayout(fluent::basicinput::Button::IconOnly);
        button->setCheckable(true);
        button->setFixedSize(44, 36);
        toolGroup->addButton(button);
        toolLayout->addWidget(button);
        connect(
            button, &fluent::basicinput::Button::clicked, this, [this, tool]() { m_viewModel->setActiveTool(tool); });
        return button;
    };
    addCanvasTool(QStringLiteral("canvasSelectButton"),
                  Typography::Icons::glyph(QStringLiteral("ic_fluent_cursor_20_regular")),
                  tr("选择区域"),
                  CanvasTool::Select);
    addCanvasTool(QStringLiteral("canvasPanButton"),
                  Typography::Icons::glyph(QStringLiteral("ic_fluent_hand_left_20_regular")),
                  tr("平移画布"),
                  CanvasTool::Pan);
    addCanvasTool(QStringLiteral("canvasCreateRegionButton"),
                  Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular")),
                  tr("创建文本区域"),
                  CanvasTool::CreateRegion);
    toolRail->move(12, 12);

    m_canvasStatusBar = new QWidget(canvasPaneCard);
    m_canvasStatusBar->setObjectName(QStringLiteral("editorCanvasStatusBar"));
    m_canvasStatusBar->setFixedHeight(36);
    auto* statusLayout = new QHBoxLayout(m_canvasStatusBar);
    statusLayout->setContentsMargins(8, 2, 8, 2);
    statusLayout->setSpacing(4);
    const auto addStatusButton =
        [this, statusLayout](const QString& objectName, const QString& glyph, const QString& label, auto callback) {
            auto* button = new fluent::basicinput::Button(statusLayout->parentWidget());
            button->setObjectName(objectName);
            button->setIconGlyph(glyph, Typography::IconSize::Standard);
            button->setAccessibleName(label);
            button->setToolTip(label);
            button->setFluentStyle(fluent::basicinput::Button::Subtle);
            button->setFluentLayout(fluent::basicinput::Button::IconOnly);
            button->setFixedSize(32, 32);
            statusLayout->addWidget(button);
            connect(button, &fluent::basicinput::Button::clicked, this, callback);
        };
    addStatusButton(QStringLiteral("canvasZoomOutButton"), Typography::Icons::ZoomOut, tr("缩小"), [this]() {
        m_canvas->zoomOut();
    });
    m_canvasZoomValueLabel = new fluent::textfields::Label(m_canvasStatusBar);
    m_canvasZoomValueLabel->setObjectName(QStringLiteral("canvasZoomValueLabel"));
    m_canvasZoomValueLabel->setMinimumWidth(48);
    m_canvasZoomValueLabel->setAlignment(Qt::AlignCenter);
    statusLayout->addWidget(m_canvasZoomValueLabel);
    addStatusButton(
        QStringLiteral("canvasZoomInButton"), Typography::Icons::ZoomIn, tr("放大"), [this]() { m_canvas->zoomIn(); });
    addStatusButton(QStringLiteral("canvasFitButton"), Typography::Icons::Zoom, tr("适合页面"), [this]() {
        m_canvas->fitToPage();
    });
    statusLayout->addStretch(1);
    m_canvasPageSizeLabel = new fluent::textfields::Label(m_canvasStatusBar);
    m_canvasPageSizeLabel->setObjectName(QStringLiteral("canvasPageSizeLabel"));
    m_canvasPageSizeLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    statusLayout->addWidget(m_canvasPageSizeLabel);
    canvasPaneLayout->addWidget(canvasHost, 1);
    canvasPaneLayout->addWidget(m_canvasStatusBar);
    m_splitter->addWidget(canvasPaneCard);

    auto* inspectorCard = new widget::PaneCard(m_splitter);
    inspectorCard->setObjectName(QStringLiteral("editorInspectorCard"));
    inspectorCard->setAppearance(fluent::layout::Card::Layer);
    inspectorCard->setMaximumWidth(kInspectorMaxWidth);
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

    m_splitter->refreshHandlePainting();
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

    m_inspector->setOcrAvailability(state.isOcrAvailable, state.ocrUnavailableReason);
    m_inspector->setTranslationAvailability(state.isTranslationAvailable, state.translationUnavailableReason);
    m_inspector->setProcessingState(state.processing);
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
    if (m_canvasToolRail != nullptr) {
        if (auto* button =
                m_canvasToolRail->findChild<fluent::basicinput::Button*>(QStringLiteral("canvasSelectButton"))) {
            button->setChecked(state.activeTool == CanvasTool::Select);
        }
        if (auto* button =
                m_canvasToolRail->findChild<fluent::basicinput::Button*>(QStringLiteral("canvasPanButton"))) {
            button->setChecked(state.activeTool == CanvasTool::Pan);
        }
        if (auto* button =
                m_canvasToolRail->findChild<fluent::basicinput::Button*>(QStringLiteral("canvasCreateRegionButton"))) {
            button->setChecked(state.activeTool == CanvasTool::CreateRegion);
        }
    }

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
    m_canvas->setPreviewMode(static_cast<int>(state.previewMode));
    m_viewModel->requestPreview(state.currentPageId);

    for (const auto& p : state.pages) {
        if (p.id == state.currentPageId) {
            m_canvas->setPageBounds(p.imageSize);
            if (m_canvasPageSizeLabel != nullptr) {
                m_canvasPageSizeLabel->setText(tr("%1 × %2 px").arg(p.imageSize.width).arg(p.imageSize.height));
            }
            break;
        }
    }
    if (m_canvasZoomValueLabel != nullptr) {
        m_canvasZoomValueLabel->setText(QStringLiteral("%1%").arg(qRound(state.zoomFactor * 100.0)));
    }

    if (!state.projectId.isValid()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoProject;
        m_canvas->setContentState(m_lastPlaceholder, tr("打开或创建项目以开始编辑"), tr("打开项目"));
    } else if (state.pages.isEmpty()) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::NoPage;
        m_canvas->setContentState(m_lastPlaceholder, tr("项目中还没有页面"), tr("导入页面"));
    } else if (state.notice) {
        m_lastPlaceholder = ui::manga::MangaCanvas::ContentState::Error;
        m_canvas->setContentState(m_lastPlaceholder,
                                  state.notice->safeMessage,
                                  state.notice->action == application::RecoveryAction::ChoosePath
                                      ? state.notice->actionText
                                      : (state.notice->retryable ? tr("重试加载") : QString()));
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
            if (m_viewModel->state().notice.has_value() &&
                m_viewModel->state().notice->action == application::RecoveryAction::ChoosePath) {
                Q_EMIT chooseExportPathRequested();
            } else {
                m_viewModel->retryPageLoad();
            }
            break;
        default:
            break;
    }
}

} // namespace manga::ui::screen::editor
