#include "ui/widget/EditorCommandBar.h"

#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/FluentQt.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>

#include <initializer_list>

using namespace manga::ui::screen::editor;

namespace manga::ui::widget {

EditorCommandBar::EditorCommandBar(EditorViewModel* viewModel, QWidget* parent)
    : QWidget(parent), m_viewModel(viewModel) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // docs/ui/pages/editor.md keeps the command bar on a single row at every width, so the height is
    // fixed here.
    setFixedHeight(40);
    m_outputDirProvider = [this]() { return QFileDialog::getExistingDirectory(this, tr("选择导出目录")); };
    buildUi();
    setupShortcuts();

    connect(m_viewModel, &EditorViewModel::stateChanged, this, &EditorCommandBar::render);
    render(m_viewModel->state());
}

bool EditorCommandBar::shouldIgnoreShortcut() const {
    if (!m_viewModel->state().currentPageId.isValid()) {
        return true;
    }
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw) || qobject_cast<QPlainTextEdit*>(fw);
}

void EditorCommandBar::buildUi() {
    auto* layout = new QHBoxLayout(this);
    // Insets and gaps use the 4 px micro spacing step from docs/ui/foundations.md.
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_undoButton = createIconButton(Typography::Icons::Undo, tr("撤销 (Ctrl+Z)"), QStringLiteral("undoButton"));
    m_redoButton = createIconButton(Typography::Icons::Redo, tr("重做 (Ctrl+Y)"), QStringLiteral("redoButton"));

    m_zoomOutButton = createIconButton(Typography::Icons::ZoomOut, tr("缩小 (-)"), QStringLiteral("zoomOutButton"));
    m_zoomValueLabel = new fluent::textfields::Label(this);
    m_zoomValueLabel->setObjectName(QStringLiteral("zoomValueLabel"));
    m_zoomValueLabel->setFluentTypography(Typography::FontRole::Caption);
    m_zoomValueLabel->setMinimumWidth(44);
    m_zoomValueLabel->setAlignment(Qt::AlignCenter);
    m_zoomInButton = createIconButton(Typography::Icons::ZoomIn, tr("放大 (+)"), QStringLiteral("zoomInButton"));
    m_fitToPageButton =
        createIconButton(Typography::Icons::Zoom, tr("适应页面 (0)"), QStringLiteral("fitToPageButton"));
    m_actualSizeButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_text_font_size_20_regular")),
                         tr("实际大小 (1)"),
                         QStringLiteral("actualSizeButton"));

    m_selectButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_cursor_20_regular")),
                                      tr("选择工具 (V)"),
                                      QStringLiteral("selectButton"));
    m_selectButton->setCheckable(true);
    m_panButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_hand_left_20_regular")),
                                   tr("平移工具 (H)"),
                                   QStringLiteral("panButton"));
    m_panButton->setCheckable(true);
    m_textButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_text_add_t_20_regular")),
                                    tr("文字框 (T)"),
                                    QStringLiteral("textButton"));
    m_textButton->setCheckable(true);
    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addButton(m_selectButton);
    toolGroup->addButton(m_panButton);
    toolGroup->addButton(m_textButton);
    m_previewSelectorButton =
        createIconButton(Typography::Icons::View, tr("预览模式"), QStringLiteral("previewSelectorButton"));
    m_previewSelectorButton->setFluentLayout(fluent::basicinput::Button::IconAfter);
    m_previewSelectorButton->setFixedWidth(84);
    m_detectButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular")),
                                      tr("文本检测 (整页)"),
                                      QStringLiteral("detectButton"));
    m_ocrButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_type_20_regular")),
                                   tr("文字识别 (整页)"),
                                   QStringLiteral("ocrButton"));
    m_inpaintButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_sparkle_action_20_regular")),
                                       tr("文字消除 (整页)"),
                                       QStringLiteral("inpaintButton"));
    m_translateButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular")),
                                         tr("AI 翻译 (Ctrl+T)"),
                                         QStringLiteral("translateButton"));
    m_smartTranslateButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_play_12_regular")),
                                              tr("全页智能翻译"),
                                              QStringLiteral("smartTranslateButton"));
    m_pagePaneToggleButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_panel_left_text_20_regular")),
                         tr("页面列表"),
                         QStringLiteral("pagePaneToggleButton"));
    m_pagePaneToggleButton->setCheckable(true);
    m_pagePaneToggleButton->setVisible(false);
    m_inspectorPaneToggleButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_panel_right_20_regular")),
                         tr("属性面板"),
                         QStringLiteral("inspectorPaneToggleButton"));
    m_inspectorPaneToggleButton->setCheckable(true);
    m_inspectorPaneToggleButton->setVisible(false);
    m_saveButton = createIconButton(Typography::Icons::Save, tr("保存项目 (Ctrl+S)"), QStringLiteral("saveButton"));
    m_overflowButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_more_horizontal_20_regular")),
                         tr("更多命令"),
                         QStringLiteral("overflowButton"));
    m_overflowButton->hide();
    m_overflowMenu = new fluent::menus_toolbars::FluentMenu(QString(), this);
    const auto addOverflowAction = [this](const QString& text, fluent::basicinput::Button* button) {
        auto* action = m_overflowMenu->addAction(text);
        connect(action, &QAction::triggered, button, &fluent::basicinput::Button::click);
        return action;
    };
    m_zoomOutOverflowAction = addOverflowAction(tr("缩小"), m_zoomOutButton);
    m_zoomInOverflowAction = addOverflowAction(tr("放大"), m_zoomInButton);
    m_fitToPageOverflowAction = addOverflowAction(tr("适应页面"), m_fitToPageButton);
    m_actualSizeOverflowAction = addOverflowAction(tr("实际大小"), m_actualSizeButton);
    m_previewOverflowAction = addOverflowAction(tr("切换原图/译文预览"), m_previewSelectorButton);
    connect(m_overflowButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_overflowMenu->exec(m_overflowButton->mapToGlobal(QPoint(0, m_overflowButton->height())));
    });

    bool hasGroup = false;
    const auto addGroup = [this, layout, &hasGroup](std::initializer_list<QWidget*> widgets) {
        QFrame* separator = nullptr;
        if (hasGroup) {
            layout->addSpacing(6);
            separator = new QFrame(layout->parentWidget());
            separator->setObjectName(QStringLiteral("editorCommandBarSeparator%1").arg(m_commandSeparators.size()));
            separator->setFrameShape(QFrame::NoFrame);
            separator->setFixedSize(1, 16);
            separator->setStyleSheet(QStringLiteral("background-color: rgba(128, 128, 128, 0.25);"));
            layout->addWidget(separator, 0, Qt::AlignVCenter);
            layout->addSpacing(6);
        }
        auto* group = new QWidget(layout->parentWidget());
        auto* groupLayout = new QHBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(2);
        for (auto* widget : widgets) {
            groupLayout->addWidget(widget);
        }
        layout->addWidget(group);
        m_commandGroups.append(group);
        m_commandGroupWidgets.append(QVector<QWidget*>(widgets));
        m_commandSeparators.append(separator);
        hasGroup = true;
    };

    addGroup({m_undoButton, m_redoButton});
    m_previewSelectorButton->hide();
    m_previewOverflowAction->setVisible(false);
    addGroup({m_detectButton, m_ocrButton, m_inpaintButton, m_translateButton});
    layout->addStretch();
    hasGroup = false;
    addGroup({m_pagePaneToggleButton, m_inspectorPaneToggleButton});
    addGroup({m_overflowButton});
    addGroup({m_smartTranslateButton});
    addGroup({m_saveButton});

    connect(m_undoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::undo);
    connect(m_redoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::redo);
    connect(m_zoomOutButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT zoomOutRequested(); });
    connect(m_zoomInButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT zoomInRequested(); });
    connect(m_fitToPageButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT fitToPageRequested(); });
    connect(m_actualSizeButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT actualSizeRequested(); });
    connect(m_selectButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::Select);
    });
    connect(m_panButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::Pan);
    });
    connect(m_textButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::Text);
    });
    connect(m_previewSelectorButton, &fluent::basicinput::Button::clicked, this, [this]() {
        const auto current = m_viewModel->state().previewMode;
        const auto mode = current == PreviewMode::Original
                              ? PreviewMode::Translated
                              : (current == PreviewMode::Translated ? PreviewMode::Compare : PreviewMode::Original);
        m_viewModel->setPreviewMode(mode);
    });
    connect(m_detectButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (!state.processing.isRunning && state.currentPageId.isValid()) {
                if (!state.regions.isEmpty()) {
                    auto* dialog = new fluent::dialogs_flyouts::ContentDialog(window());
                    dialog->setObjectName(QStringLiteral("detectConfirmDialog"));
                    dialog->setTitle(tr("重新检测区域"));
                    auto* message = new fluent::textfields::Label(
                        tr("当前页面已有文本区域。重新检测将清除这些区域。\n此操作可以撤销。是否继续？"));
                    message->setFluentTypography(Typography::FontRole::Body);
                    message->setWordWrap(true);
                    message->setTextColorRole(fluent::textfields::Label::TextColorRole::Primary);
                    dialog->setContent(message);
                    dialog->setPrimaryButtonText(tr("重新检测"));
                    dialog->setCloseButtonText(tr("取消"));
                    dialog->setDefaultButton(fluent::dialogs_flyouts::ContentDialog::Primary);
                    dialog->setWindowModality(Qt::WindowModal);

                    connect(
                        dialog, &fluent::dialogs_flyouts::ContentDialog::primaryButtonClicked, this, [this, ctrl]() {
                            const auto& s = m_viewModel->state();
                            if (!s.processing.isRunning && s.currentPageId.isValid()) {
                                ctrl->detectRegions(s.currentPageId, true);
                            }
                        });
                    connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
                    dialog->open();
                } else {
                    ctrl->detectRegions(state.currentPageId, true);
                }
            }
        }
    });
    connect(m_ocrButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (!state.processing.isRunning && state.currentPageId.isValid() && !state.regions.isEmpty()) {
                ctrl->recognizePage(state.currentPageId);
            }
        }
    });
    connect(m_inpaintButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (!state.processing.isRunning && state.currentPageId.isValid() && !state.regions.isEmpty()) {
                ctrl->clearPage(state.currentPageId);
            }
        }
    });
    connect(m_translateButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (!state.processing.isRunning && state.currentPageId.isValid()) {
                if (state.selectedRegionIds.isEmpty()) {
                    ctrl->translatePage(state.currentPageId);
                } else {
                    ctrl->translateRegions(state.currentPageId, state.selectedRegionIds);
                }
            }
        }
    });
    connect(m_smartTranslateButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (state.currentPageId.isValid()) {
                if (state.processing.isRunning) {
                    ctrl->cancelPageOperations(state.currentPageId);
                } else {
                    ctrl->smartTranslatePage(state.currentPageId);
                }
            }
        }
    });
    connect(m_pagePaneToggleButton, &fluent::basicinput::Button::clicked, this, [this]() {
        Q_EMIT pagePaneToggleRequested();
    });
    connect(m_inspectorPaneToggleButton, &fluent::basicinput::Button::clicked, this, [this]() {
        Q_EMIT inspectorPaneToggleRequested();
    });
    connect(m_saveButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::saveProject);
}

fluent::basicinput::Button*
EditorCommandBar::createIconButton(const QString& glyph, const QString& tooltip, const QString& name) {
    auto* btn = new fluent::basicinput::Button(this);
    btn->setObjectName(name);
    btn->setFluentStyle(fluent::basicinput::Button::Subtle);
    btn->setFluentSize(fluent::basicinput::Button::StandardSize);
    btn->setFluentLayout(fluent::basicinput::Button::IconOnly);
    btn->setFixedSize(32, 32);
    btn->setIconGlyph(glyph, Typography::IconSize::Standard);
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    return btn;
}

void EditorCommandBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = themeColorsRef();
    painter.fillRect(rect(), colors.bgLayer);
    painter.setPen(QPen(colors.strokeDivider, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void EditorCommandBar::onThemeUpdated() {
    update();
}

void EditorCommandBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void EditorCommandBar::updateResponsiveLayout() {
    const bool overflowActive = width() < 1008;
    m_overflowActive = overflowActive;

    const bool narrow = width() < 720;
    Q_UNUSED(narrow);
    m_previewSelectorButton->setVisible(false);
    m_overflowButton->setVisible(overflowActive);

    m_zoomOutOverflowAction->setVisible(narrow);
    m_zoomInOverflowAction->setVisible(narrow);
    m_fitToPageOverflowAction->setVisible(overflowActive);
    m_actualSizeOverflowAction->setVisible(overflowActive);
    m_previewOverflowAction->setVisible(false);

    // Zoom and tool controls live on the canvas surfaces, not in this row.
    m_zoomOutButton->hide();
    m_zoomValueLabel->hide();
    m_zoomInButton->hide();
    m_fitToPageButton->hide();
    m_actualSizeButton->hide();
    m_selectButton->hide();
    m_panButton->hide();
    m_textButton->hide();

    bool previousGroupVisible = false;
    for (int i = 0; i < m_commandGroups.size(); ++i) {
        if (m_commandSeparators.at(i) == nullptr) {
            previousGroupVisible = false;
        }
        const auto& children = m_commandGroupWidgets.at(i);
        bool groupVisible = false;
        for (const auto* child : children) {
            if (!child->isHidden()) {
                groupVisible = true;
                break;
            }
        }
        m_commandGroups.at(i)->setVisible(groupVisible);
        if (m_commandSeparators.at(i) != nullptr) {
            m_commandSeparators.at(i)->setVisible(groupVisible && previousGroupVisible);
        }
        previousGroupVisible = previousGroupVisible || groupVisible;
    }
}

void EditorCommandBar::render(const EditorState& state) {
    const bool isRunning = state.processing.isRunning;
    m_undoButton->setEnabled(state.canUndo && !isRunning);
    m_redoButton->setEnabled(state.canRedo && !isRunning);
    m_saveButton->setEnabled(state.saveState.isDirty && !state.saveState.isSaving && !isRunning);

    m_zoomValueLabel->setText(QStringLiteral("%1%").arg(qRound(state.zoomFactor * 100.0)));

    const QString previewText = state.previewMode == PreviewMode::Original
                                    ? tr("原图")
                                    : (state.previewMode == PreviewMode::Translated ? tr("翻译") : tr("对比"));
    m_previewSelectorButton->setText(previewText);
    m_previewSelectorButton->setToolTip(tr("预览模式：%1（点击切换）").arg(previewText));
    m_previewSelectorButton->setAccessibleName(tr("预览模式：%1").arg(previewText));

    m_selectButton->setChecked(state.activeTool == CanvasTool::Select);
    m_panButton->setChecked(state.activeTool == CanvasTool::Pan);
    m_textButton->setChecked(state.activeTool == CanvasTool::Text);

    // Disable viewport and canvas tools if no page is active
    const bool hasPage = state.currentPageId.isValid() && !state.isPageLoading;
    const auto detectionReason = processingUnavailableReason(state, application::OperationStage::Detect);
    const auto ocrReason = processingUnavailableReason(state, application::OperationStage::Ocr);
    const auto inpaintReason = processingUnavailableReason(state, application::OperationStage::Clear);
    const auto translationReason = processingUnavailableReason(state, application::OperationStage::Translate);
    const auto smartReason = !ocrReason.isEmpty()           ? ocrReason
                             : !translationReason.isEmpty() ? translationReason
                                                            : inpaintReason;
    const bool hasRegions = !state.regions.isEmpty();
    m_zoomOutButton->setEnabled(hasPage);
    m_zoomInButton->setEnabled(hasPage);
    m_fitToPageButton->setEnabled(hasPage);
    m_actualSizeButton->setEnabled(hasPage);
    m_selectButton->setEnabled(hasPage);
    m_panButton->setEnabled(hasPage);
    m_textButton->setEnabled(hasPage);
    m_previewSelectorButton->setEnabled(hasPage);
    const bool hasSelectedRegions = hasPage && !state.selectedRegionIds.isEmpty();

    // 1. Detection: only for whole page (hidden when text boxes are selected)
    m_detectButton->setVisible(!hasSelectedRegions);
    m_detectButton->setEnabled(hasPage && !isRunning && detectionReason.isEmpty());
    m_detectButton->setToolTip(!detectionReason.isEmpty() ? detectionReason
                               : isRunning                ? tr("正在执行处理任务...")
                               : !hasPage                 ? tr("请先打开页面并等待加载完成")
                                                          : tr("检测整页文字区域"));
    m_detectButton->setAccessibleName(tr("文本检测 (整页)"));

    // 2. OCR: only for whole page (hidden when text boxes are selected)
    m_ocrButton->setVisible(!hasSelectedRegions);
    m_ocrButton->setEnabled(hasPage && hasRegions && !isRunning && state.isOcrAvailable);
    if (isRunning) {
        m_ocrButton->setToolTip(tr("正在执行处理任务..."));
    } else if (!state.isOcrAvailable) {
        m_ocrButton->setToolTip(ocrReason);
    } else if (!hasRegions) {
        m_ocrButton->setToolTip(tr("当前页面没有文字区域，请先检测或创建区域"));
    } else {
        m_ocrButton->setToolTip(tr("识别整页文字 (OCR)"));
    }
    m_ocrButton->setAccessibleName(tr("文字识别 (整页)"));

    // 3. Inpaint: only for whole page (hidden when text boxes are selected)
    m_inpaintButton->setVisible(!hasSelectedRegions);
    m_inpaintButton->setEnabled(hasPage && hasRegions && !isRunning && inpaintReason.isEmpty());
    if (isRunning) {
        m_inpaintButton->setToolTip(tr("正在执行处理任务..."));
    } else if (!inpaintReason.isEmpty()) {
        m_inpaintButton->setToolTip(inpaintReason);
    } else if (!hasRegions) {
        m_inpaintButton->setToolTip(tr("当前页面没有文字区域"));
    } else {
        m_inpaintButton->setToolTip(tr("消除整页文字背景 (Inpaint)"));
    }
    m_inpaintButton->setAccessibleName(tr("文字消除 (整页)"));

    // 4. Translate: dual mode (always visible, whole page vs selected)
    m_translateButton->setVisible(true);
    m_translateButton->setEnabled(hasPage && (hasSelectedRegions || hasRegions) && !isRunning &&
                                  state.isTranslationAvailable);
    if (isRunning) {
        m_translateButton->setToolTip(tr("正在执行处理任务..."));
    } else if (!state.isTranslationAvailable) {
        m_translateButton->setToolTip(translationReason);
    } else if (hasSelectedRegions) {
        m_translateButton->setToolTip(tr("翻译所选文字区域 (%1 个) (Ctrl+T)").arg(state.selectedRegionIds.size()));
    } else if (!hasRegions) {
        m_translateButton->setToolTip(tr("当前页无文字区域，请先创建或检测区域"));
    } else {
        m_translateButton->setToolTip(tr("翻译整页已存在文字区域 (Ctrl+T)"));
    }
    m_translateButton->setAccessibleName(hasSelectedRegions ? tr("翻译所选文字") : tr("整页翻译"));

    // 5. Smart Translate (Whole-page automated pipeline)
    m_smartTranslateButton->setVisible(true);
    if (isRunning) {
        m_smartTranslateButton->setEnabled(true);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_pause_12_regular")),
                                             Typography::IconSize::Standard);
        m_smartTranslateButton->setToolTip(tr("停止翻译"));
        m_smartTranslateButton->setAccessibleName(tr("停止翻译"));
    } else {
        const bool canSmartTranslate = hasPage && hasRegions && smartReason.isEmpty();
        m_smartTranslateButton->setEnabled(canSmartTranslate);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_play_12_regular")),
                                             Typography::IconSize::Standard);
        if (!hasPage) {
            m_smartTranslateButton->setToolTip(tr("请先打开页面"));
        } else if (!smartReason.isEmpty()) {
            m_smartTranslateButton->setToolTip(smartReason);
        } else if (!hasRegions) {
            m_smartTranslateButton->setToolTip(tr("当前页面没有文字区域，请先检测或创建区域"));
        } else {
            m_smartTranslateButton->setToolTip(tr("全页智能翻译"));
        }
        m_smartTranslateButton->setAccessibleName(tr("全页智能翻译"));
    }

    updateResponsiveLayout();
}

void EditorCommandBar::setupShortcuts() {
    auto makeGlobal = [this](const QString& seq, auto func) {
        auto* s = new QShortcut(QKeySequence(seq), this);
        s->setContext(Qt::ApplicationShortcut);
        connect(s, &QShortcut::activated, this, [this, func]() {
            if (!shouldIgnoreShortcut()) {
                func();
            }
        });
    };

    makeGlobal(QStringLiteral("V"), [this]() { m_viewModel->setActiveTool(CanvasTool::Select); });
    makeGlobal(QStringLiteral("H"), [this]() { m_viewModel->setActiveTool(CanvasTool::Pan); });
    makeGlobal(QStringLiteral("T"), [this]() { m_viewModel->setActiveTool(CanvasTool::Text); });
    makeGlobal(QStringLiteral("B"), [this]() { m_viewModel->setActiveTool(CanvasTool::Brush); });
    makeGlobal(QStringLiteral("E"), [this]() { m_viewModel->setActiveTool(CanvasTool::Eraser); });
    makeGlobal(QStringLiteral("J"), [this]() { m_viewModel->setActiveTool(CanvasTool::Remove); });

    makeGlobal(QStringLiteral("+"), [this]() { Q_EMIT zoomInRequested(); });
    makeGlobal(QStringLiteral("="), [this]() { Q_EMIT zoomInRequested(); });
    makeGlobal(QStringLiteral("-"), [this]() { Q_EMIT zoomOutRequested(); });
    makeGlobal(QStringLiteral("0"), [this]() { Q_EMIT fitToPageRequested(); });
    makeGlobal(QStringLiteral("1"), [this]() { Q_EMIT actualSizeRequested(); });
    makeGlobal(QStringLiteral("Delete"), [this]() { Q_EMIT deleteSelectedRegionsRequested(); });
    makeGlobal(QStringLiteral("Return"), [this]() { Q_EMIT commitRequested(); });
    makeGlobal(QStringLiteral("Enter"), [this]() { Q_EMIT commitRequested(); });
    makeGlobal(QStringLiteral("Ctrl+T"), [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (state.isTranslationAvailable && !state.processing.isRunning && state.currentPageId.isValid() &&
                !state.regions.isEmpty()) {
                if (state.selectedRegionIds.isEmpty()) {
                    ctrl->translatePage(state.currentPageId);
                } else {
                    ctrl->translateRegions(state.currentPageId, state.selectedRegionIds);
                }
            }
        }
    });

    auto* sSave = new QShortcut(QKeySequence::Save, this);
    sSave->setContext(Qt::ApplicationShortcut);
    connect(sSave, &QShortcut::activated, m_viewModel, &EditorViewModel::saveProject);

    auto* sUndo = new QShortcut(QKeySequence::Undo, this);
    sUndo->setContext(Qt::ApplicationShortcut);
    connect(sUndo, &QShortcut::activated, m_viewModel, &EditorViewModel::undo);

    auto* sRedo = new QShortcut(QKeySequence::Redo, this);
    sRedo->setContext(Qt::ApplicationShortcut);
    connect(sRedo, &QShortcut::activated, m_viewModel, &EditorViewModel::redo);
}

} // namespace manga::ui::widget
