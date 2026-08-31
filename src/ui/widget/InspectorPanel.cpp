#include "ui/widget/InspectorPanel.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Layout.h>
#include <FluentQt/Scrolling.h>
#include <FluentQt/TextFields.h>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cmath>

namespace manga::ui::widget {

namespace {

constexpr int kPanelMinimumWidth = 300;
constexpr int kSwatchExtent = 28;

using screen::editor::InspectorField;

const QRegularExpression& hexColorPattern() {
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return pattern;
}

QString percentText(double confidence) {
    return QString::number(qRound(confidence * 100.0)) + QStringLiteral("%");
}

/// Replays projected text into an Inspector text control. An unchanged value is the
/// echo of the control's own commit and must not disturb cursor or selection; a real
/// change restores the previous cursor/selection clamped to the new text length.
void setTextPreservingCursor(fluent::textfields::TextEdit* edit, const QString& text) {
    if (edit->toPlainText() == text) {
        return;
    }
    auto* inner = edit->findChild<QTextEdit*>();
    if (inner == nullptr) {
        edit->setPlainText(text);
        return;
    }
    const QTextCursor previous = inner->textCursor();
    const int position = previous.position();
    const int anchor = previous.anchor();
    edit->setPlainText(text);
    QTextCursor restored = inner->textCursor();
    restored.setPosition(qMin(anchor, text.size()));
    restored.setPosition(qMin(position, text.size()), QTextCursor::KeepAnchor);
    inner->setTextCursor(restored);
}

// Orientation and alignment combo rows are inserted in domain enum ordinal order;
// intent payloads carry the ordinal. These pins keep insertion order and domain
// ordinals from drifting apart.
static_assert(static_cast<int>(domain::TextOrientation::Horizontal) == 0 &&
                  static_cast<int>(domain::TextOrientation::Vertical) == 1,
              "orientation combo rows mirror TextOrientation ordinals");
static_assert(static_cast<int>(domain::HorizontalAlignment::Left) == 0 &&
                  static_cast<int>(domain::HorizontalAlignment::Center) == 1 &&
                  static_cast<int>(domain::HorizontalAlignment::Right) == 2 &&
                  static_cast<int>(domain::HorizontalAlignment::Justify) == 3,
              "horizontal alignment combo rows mirror HorizontalAlignment ordinals");
static_assert(static_cast<int>(domain::VerticalAlignment::Top) == 0 &&
                  static_cast<int>(domain::VerticalAlignment::Middle) == 1 &&
                  static_cast<int>(domain::VerticalAlignment::Bottom) == 2,
              "vertical alignment combo rows mirror VerticalAlignment ordinals");

} // namespace

/// Encapsulated raw Qt control that paints one region content color as a swatch.
/// The fill is content data; the border consumes the Fluent stroke token so the
/// swatch stays visible on both themes.
class ColorSwatchButton : public QAbstractButton {
public:
    explicit ColorSwatchButton(QWidget* parent = nullptr) : QAbstractButton(parent) {
        setFixedSize(kSwatchExtent, kSwatchExtent);
        setCursor(Qt::PointingHandCursor);
    }

    void setSwatchColor(const QColor& color) {
        if (m_color != color) {
            m_color = color;
            update();
        }
    }
    [[nodiscard]] QColor swatchColor() const {
        return m_color;
    }
    void setBorderColor(const QColor& color) {
        if (m_borderColor != color) {
            m_borderColor = color;
            update();
        }
    }

    [[nodiscard]] QSize sizeHint() const override {
        return {kSwatchExtent, kSwatchExtent};
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF rect = QRectF(this->rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        painter.setPen(QPen(m_borderColor, 1.0));
        painter.setBrush(m_color.isValid() ? QBrush(m_color) : Qt::NoBrush);
        painter.drawRoundedRect(rect, 4.0, 4.0);
        if (!m_color.isValid()) {
            painter.drawLine(rect.bottomLeft(), rect.topRight());
        }
    }

private:
    QColor m_color;
    QColor m_borderColor{Qt::gray};
};

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    setInspectorState(screen::editor::PageInspectorState{});
}

void InspectorPanel::buildUi() {
    using fluent::layout::Field;
    using fluent::textfields::Label;

    setMinimumWidth(kPanelMinimumWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(4);

    m_headerTitle = new Label(this);
    m_headerTitle->setObjectName(QStringLiteral("inspectorHeaderTitle"));
    m_headerTitle->setFluentTypography(Typography::FontRole::Subtitle);
    root->addWidget(m_headerTitle);

    m_headerSubtitle = new Label(this);
    m_headerSubtitle->setObjectName(QStringLiteral("inspectorHeaderSubtitle"));
    m_headerSubtitle->setTextColorRole(Label::TextColorRole::Secondary);
    m_headerSubtitle->setTextElideMode(Qt::ElideMiddle);
    root->addWidget(m_headerSubtitle);

    m_scrollView = new fluent::scrolling::ScrollView(this);
    m_scrollView->setWidgetResizable(true);
    m_scrollView->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
    root->addWidget(m_scrollView, 1);

    m_stack = new QStackedWidget();
    m_pageHost = buildPageHost();
    m_singleHost = buildSingleHost();
    m_multiHost = buildMultiHost();
    m_stack->addWidget(m_pageHost);
    m_stack->addWidget(m_singleHost);
    m_stack->addWidget(m_multiHost);
    m_stack->setCurrentWidget(m_pageHost);
    m_scrollView->setContentWidget(m_stack, fluent::WidgetOwnership::Owned);
}

QWidget* InspectorPanel::buildPageHost() {
    using fluent::textfields::Label;

    auto* host = new QWidget();
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(8);

    m_pageSummaryLabel = new Label(host);
    m_pageSummaryLabel->setObjectName(QStringLiteral("inspectorPageSummary"));
    m_pageSummaryLabel->setWordWrap(true);
    layout->addWidget(m_pageSummaryLabel);

    m_pageGuidanceLabel = new Label(host);
    m_pageGuidanceLabel->setObjectName(QStringLiteral("inspectorPageGuidance"));
    m_pageGuidanceLabel->setTextColorRole(Label::TextColorRole::Secondary);
    m_pageGuidanceLabel->setWordWrap(true);
    layout->addWidget(m_pageGuidanceLabel);

    m_pageShortcutsLabel = new Label(host);
    m_pageShortcutsLabel->setObjectName(QStringLiteral("inspectorPageShortcuts"));
    m_pageShortcutsLabel->setTextColorRole(Label::TextColorRole::Tertiary);
    m_pageShortcutsLabel->setWordWrap(true);
    layout->addWidget(m_pageShortcutsLabel);

    layout->addStretch(1);
    return host;
}

namespace {

/// Builds one labeled numeric row. The Field owns the NumberBox.
std::pair<fluent::layout::Field*, fluent::textfields::NumberBox*>
makeNumericRow(const QString& label, double minimum, double maximum, int precision, QVBoxLayout* parentLayout) {
    auto* field = new fluent::layout::Field();
    field->setLabelText(label);
    auto* box = new fluent::textfields::NumberBox();
    box->setRange(minimum, maximum);
    box->setDisplayPrecision(precision);
    box->setSpinButtonPlacementMode(fluent::textfields::NumberBox::SpinButtonPlacementMode::Compact);
    field->setEditor(box, fluent::WidgetOwnership::Owned);
    parentLayout->addWidget(field);
    return {field, box};
}

void clearFieldError(fluent::layout::Field* field) {
    field->setValidationState(fluent::layout::Field::ValidationState::None);
    field->setValidationMessage(QString());
}

} // namespace

QWidget* InspectorPanel::buildSingleHost() {
    using fluent::basicinput::Button;
    using fluent::basicinput::ComboBox;
    using fluent::layout::Expander;
    using fluent::layout::Field;
    using fluent::textfields::Label;
    using fluent::textfields::LineEdit;
    using fluent::textfields::NumberBox;
    using fluent::textfields::TextEdit;

    auto* host = new QWidget();
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(12);

    // Translation section (expanded by default).
    m_translationExpander = new Expander(host);
    m_translationExpander->setHeaderText(tr("翻译"));
    auto* translationContent = new QWidget();
    auto* translationLayout = new QVBoxLayout(translationContent);
    translationLayout->setContentsMargins(12, 8, 12, 12);
    translationLayout->setSpacing(8);

    auto* sourceField = new Field();
    sourceField->setLabelText(tr("原文"));
    m_sourceEdit = new TextEdit();
    m_sourceEdit->setObjectName(QStringLiteral("inspectorSourceEdit"));
    m_sourceEdit->setMinVisibleLines(2);
    m_sourceEdit->setMaxVisibleLines(5);
    m_sourceEdit->setAccessibleName(tr("原文"));
    sourceField->setEditor(m_sourceEdit, fluent::WidgetOwnership::Owned);
    translationLayout->addWidget(sourceField);
    connect(m_sourceEdit, &TextEdit::textChanged, this, [this]() {
        if (!m_updating && !m_inImePreedit) {
            emitFieldEdit(InspectorField::SourceText, m_sourceEdit->toPlainText());
        }
    });

    m_sourceMetaLabel = new Label();
    m_sourceMetaLabel->setObjectName(QStringLiteral("inspectorSourceMeta"));
    m_sourceMetaLabel->setTextColorRole(Label::TextColorRole::Secondary);
    translationLayout->addWidget(m_sourceMetaLabel);

    auto* translationField = new Field();
    translationField->setLabelText(tr("译文"));
    m_translationEdit = new TextEdit();
    m_translationEdit->setObjectName(QStringLiteral("inspectorTranslationEdit"));
    m_translationEdit->setMinVisibleLines(2);
    m_translationEdit->setMaxVisibleLines(5);
    m_translationEdit->setAccessibleName(tr("译文"));
    translationField->setEditor(m_translationEdit, fluent::WidgetOwnership::Owned);
    translationLayout->addWidget(translationField);
    connect(m_translationEdit, &TextEdit::textChanged, this, [this]() {
        if (!m_updating && !m_inImePreedit) {
            emitFieldEdit(InspectorField::ManualTranslation, m_translationEdit->toPlainText());
        }
    });

    // The project undo stack is the single authoritative text history. The control
    // keeps only selection, cursor and uncommitted IME preedit; FocusOut seals the
    // ViewModel merge batch via the event filter.
    for (auto* edit : {m_sourceEdit, m_translationEdit}) {
        if (auto* inner = edit->findChild<QTextEdit*>()) {
            inner->setUndoRedoEnabled(false);
            inner->installEventFilter(this);
        }
    }

    m_translationMetaLabel = new Label();
    m_translationMetaLabel->setObjectName(QStringLiteral("inspectorTranslationMeta"));
    m_translationMetaLabel->setTextColorRole(Label::TextColorRole::Secondary);
    translationLayout->addWidget(m_translationMetaLabel);

    m_candidatesExpander = new Expander();
    m_candidatesExpander->setHeaderText(tr("候选译文"));
    auto* candidatesContent = new QWidget();
    auto* candidatesLayout = new QVBoxLayout(candidatesContent);
    candidatesLayout->setContentsMargins(12, 8, 12, 12);
    candidatesLayout->setSpacing(8);
    // FluentQt ListView is avoided for interactive row lists in this project; the
    // candidates list is an encapsulated raw QListWidget fed with Fluent tokens.
    m_candidatesList = new QListWidget();
    m_candidatesList->setObjectName(QStringLiteral("inspectorCandidatesList"));
    m_candidatesList->setAccessibleName(tr("候选译文列表"));
    m_candidatesList->setMaximumHeight(160);
    candidatesLayout->addWidget(m_candidatesList);
    m_adoptCandidateButton = new Button(tr("采用候选"));
    m_adoptCandidateButton->setObjectName(QStringLiteral("inspectorAdoptCandidate"));
    m_adoptCandidateButton->setEnabled(false);
    candidatesLayout->addWidget(m_adoptCandidateButton);
    m_candidatesExpander->setContentWidget(candidatesContent, fluent::WidgetOwnership::Owned);
    translationLayout->addWidget(m_candidatesExpander);

    // Read-only clear status display; clearing actions stay outside the Inspector.
    auto* clearStatusRow = new QWidget();
    auto* clearStatusLayout = new QHBoxLayout(clearStatusRow);
    clearStatusLayout->setContentsMargins(0, 0, 0, 0);
    clearStatusLayout->setSpacing(8);
    auto* clearStatusCaption = new Label(tr("清除状态"));
    clearStatusCaption->setTextColorRole(Label::TextColorRole::Secondary);
    m_clearStatusValue = new Label();
    m_clearStatusValue->setObjectName(QStringLiteral("inspectorClearStatus"));
    clearStatusLayout->addWidget(clearStatusCaption);
    clearStatusLayout->addWidget(m_clearStatusValue, 1);
    translationLayout->addWidget(clearStatusRow);

    auto* singleActionsRow = new QWidget();
    auto* singleActionsLayout = new QHBoxLayout(singleActionsRow);
    singleActionsLayout->setContentsMargins(0, 4, 0, 4);
    singleActionsLayout->setSpacing(8);
    m_singleOcrButton = new Button(tr("OCR"));
    m_singleOcrButton->setObjectName(QStringLiteral("inspectorSingleOcr"));
    m_singleTranslateButton = new Button(tr("翻译"));
    m_singleTranslateButton->setObjectName(QStringLiteral("inspectorSingleTranslate"));
    m_singleClearButton = new Button(tr("涂抹"));
    m_singleClearButton->setObjectName(QStringLiteral("inspectorSingleClear"));
    m_singleLayoutButton = new Button(tr("排版"));
    m_singleLayoutButton->setObjectName(QStringLiteral("inspectorSingleLayout"));
    singleActionsLayout->addWidget(m_singleOcrButton);
    singleActionsLayout->addWidget(m_singleTranslateButton);
    singleActionsLayout->addWidget(m_singleClearButton);
    singleActionsLayout->addWidget(m_singleLayoutButton);
    translationLayout->addWidget(singleActionsRow);

    connect(m_singleOcrButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT ocrRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_singleTranslateButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT translateRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_singleClearButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT clearRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_singleLayoutButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT layoutRequested(m_currentPageId, m_currentRegionIds);
        }
    });

    connect(m_candidatesList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_adoptCandidateButton->setEnabled(row >= 0);
    });
    connect(m_adoptCandidateButton, &Button::clicked, this, [this]() {
        auto* item = m_candidatesList->currentItem();
        if (item != nullptr && !m_currentRegionIds.isEmpty()) {
            Q_EMIT candidateAdoptionRequested(
                m_currentPageId, m_currentRegionIds.first(), item->data(Qt::UserRole).toString());
        }
    });
    connect(m_candidatesList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr && !m_currentRegionIds.isEmpty()) {
            Q_EMIT candidateAdoptionRequested(
                m_currentPageId, m_currentRegionIds.first(), item->data(Qt::UserRole).toString());
        }
    });

    m_translationExpander->setContentWidget(translationContent, fluent::WidgetOwnership::Owned);
    m_translationExpander->setExpanded(true);
    layout->addWidget(m_translationExpander);

    // Typesetting section (expanded by default).
    m_typesettingExpander = new Expander(host);
    m_typesettingExpander->setHeaderText(tr("排版"));
    auto* typesetContent = new QWidget();
    auto* typesetLayout = new QVBoxLayout(typesetContent);
    typesetLayout->setContentsMargins(12, 8, 12, 12);
    typesetLayout->setSpacing(8);

    auto* fontFamilyField = new Field();
    fontFamilyField->setLabelText(tr("字体"));
    m_fontFamilyCombo = new ComboBox();
    m_fontFamilyCombo->setObjectName(QStringLiteral("inspectorFontFamily"));
    m_fontFamilyCombo->setEditable(true);
    m_fontFamilyCombo->addItems({QStringLiteral("Sans-Serif"),
                                 QStringLiteral("Serif"),
                                 QStringLiteral("Monospace"),
                                 QStringLiteral("Microsoft YaHei"),
                                 QStringLiteral("SimSun"),
                                 QStringLiteral("KaiTi")});
    fontFamilyField->setEditor(m_fontFamilyCombo, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(fontFamilyField);
    connect(m_fontFamilyCombo, &ComboBox::activated, this, [this](int) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::FontFamily, m_fontFamilyCombo->currentText());
        }
    });

    auto* fontWeightField = new Field();
    fontWeightField->setLabelText(tr("字重"));
    m_fontWeightCombo = new ComboBox();
    m_fontWeightCombo->setObjectName(QStringLiteral("inspectorFontWeight"));
    for (int weight = 100; weight <= 900; weight += 100) {
        m_fontWeightCombo->addItem(QString::number(weight));
    }
    fontWeightField->setEditor(m_fontWeightCombo, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(fontWeightField);
    connect(m_fontWeightCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::FontWeight, (index + 1) * 100);
        }
    });

    auto [fontSizeField, fontSizeBox] = makeNumericRow(tr("字号"), 6.0, 72.0, 1, typesetLayout);
    m_fontSizeField = fontSizeField;
    m_fontSizeBox = fontSizeBox;
    m_fontSizeField->setObjectName(QStringLiteral("inspectorFontSizeField"));
    m_fontSizeBox->setObjectName(QStringLiteral("inspectorFontSize"));
    connect(m_fontSizeBox, &NumberBox::valueChanged, this, [this](double value) {
        if (!m_updating) {
            handleNumericCommit(m_fontSizeField, InspectorField::FontSizePt, value);
        }
    });

    auto [letterSpacingField, letterSpacingBox] = makeNumericRow(tr("字距 (em)"), -0.5, 2.0, 2, typesetLayout);
    m_letterSpacingField = letterSpacingField;
    m_letterSpacingBox = letterSpacingBox;
    connect(m_letterSpacingBox, &NumberBox::valueChanged, this, [this](double value) {
        if (!m_updating) {
            handleNumericCommit(m_letterSpacingField, InspectorField::LetterSpacingEm, value);
        }
    });

    auto* orientationField = new Field();
    orientationField->setLabelText(tr("方向"));
    m_orientationCombo = new ComboBox();
    m_orientationCombo->setObjectName(QStringLiteral("inspectorOrientation"));
    m_orientationCombo->addItems({tr("横排"), tr("竖排")});
    orientationField->setEditor(m_orientationCombo, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(orientationField);
    connect(m_orientationCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::Orientation, index);
        }
    });

    auto* hAlignField = new Field();
    hAlignField->setLabelText(tr("水平对齐"));
    m_hAlignCombo = new ComboBox();
    m_hAlignCombo->setObjectName(QStringLiteral("inspectorHAlign"));
    m_hAlignCombo->addItems({tr("左对齐"), tr("居中对齐"), tr("右对齐"), tr("两端对齐")});
    hAlignField->setEditor(m_hAlignCombo, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(hAlignField);
    connect(m_hAlignCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::HorizontalAlignment, index);
        }
    });

    auto* vAlignField = new Field();
    vAlignField->setLabelText(tr("垂直对齐"));
    m_vAlignCombo = new ComboBox();
    m_vAlignCombo->setObjectName(QStringLiteral("inspectorVAlign"));
    m_vAlignCombo->addItems({tr("顶部对齐"), tr("垂直居中"), tr("底部对齐")});
    vAlignField->setEditor(m_vAlignCombo, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(vAlignField);
    connect(m_vAlignCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::VerticalAlignment, index);
        }
    });

    auto [lineSpacingField, lineSpacingBox] = makeNumericRow(tr("行距"), 0.5, 4.0, 2, typesetLayout);
    m_lineSpacingField = lineSpacingField;
    m_lineSpacingBox = lineSpacingBox;
    connect(m_lineSpacingBox, &NumberBox::valueChanged, this, [this](double value) {
        if (!m_updating) {
            handleNumericCommit(m_lineSpacingField, InspectorField::LineSpacing, value);
        }
    });

    auto* marginsRow = new QWidget();
    auto* marginsGrid = new QHBoxLayout(marginsRow);
    marginsGrid->setContentsMargins(0, 0, 0, 0);
    marginsGrid->setSpacing(8);
    const std::pair<const char*, InspectorField> marginSpecs[4] = {
        {"上", InspectorField::MarginTop},
        {"右", InspectorField::MarginRight},
        {"下", InspectorField::MarginBottom},
        {"左", InspectorField::MarginLeft},
    };
    fluent::layout::Field* marginFields[4] = {nullptr, nullptr, nullptr, nullptr};
    fluent::textfields::NumberBox* marginBoxes[4] = {nullptr, nullptr, nullptr, nullptr};
    int marginIndex = 0;
    for (const auto& [labelKey, marginField] : marginSpecs) {
        auto* field = new Field();
        field->setLabelText(tr("边距%1").arg(tr(labelKey)));
        auto* box = new NumberBox();
        box->setRange(0.0, 64.0);
        box->setDisplayPrecision(1);
        box->setSpinButtonPlacementMode(NumberBox::SpinButtonPlacementMode::Hidden);
        field->setEditor(box, fluent::WidgetOwnership::Owned);
        marginsGrid->addWidget(field);
        marginFields[marginIndex] = field;
        marginBoxes[marginIndex] = box;
        connect(box, &NumberBox::valueChanged, this, [this, field, marginField](double value) {
            if (!m_updating) {
                handleNumericCommit(field, marginField, value);
            }
        });
        ++marginIndex;
    }
    m_marginTopField = marginFields[0];
    m_marginRightField = marginFields[1];
    m_marginBottomField = marginFields[2];
    m_marginLeftField = marginFields[3];
    m_marginTopField->setObjectName(QStringLiteral("inspectorMarginTopField"));
    m_marginTopBox = marginBoxes[0];
    m_marginRightBox = marginBoxes[1];
    m_marginBottomBox = marginBoxes[2];
    m_marginLeftBox = marginBoxes[3];
    m_marginTopBox->setObjectName(QStringLiteral("inspectorMarginTop"));
    typesetLayout->addWidget(marginsRow);

    m_textColorField = new Field();
    m_textColorField->setObjectName(QStringLiteral("inspectorTextColorField"));
    m_textColorField->setLabelText(tr("文字颜色"));
    auto* textColorRow = new QWidget();
    auto* textColorLayout = new QHBoxLayout(textColorRow);
    textColorLayout->setContentsMargins(0, 0, 0, 0);
    textColorLayout->setSpacing(8);
    m_textColorSwatch = new ColorSwatchButton();
    m_textColorSwatch->setObjectName(QStringLiteral("inspectorTextColorSwatch"));
    m_textColorSwatch->setAccessibleName(tr("选择文字颜色"));
    m_textColorHexEdit = new LineEdit();
    m_textColorHexEdit->setObjectName(QStringLiteral("inspectorTextColorHex"));
    m_textColorHexEdit->setPlaceholderText(QStringLiteral("#RRGGBB"));
    m_textColorHexEdit->setAccessibleName(tr("文字颜色十六进制值"));
    textColorLayout->addWidget(m_textColorSwatch);
    textColorLayout->addWidget(m_textColorHexEdit, 1);
    m_textColorField->setEditor(textColorRow, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(m_textColorField);
    connect(m_textColorHexEdit, &LineEdit::editingFinished, this, [this]() {
        if (!m_updating) {
            handleHexCommit(m_textColorHexEdit, m_textColorField, m_textColorSwatch, InspectorField::TextColorHex);
        }
    });
    connect(m_textColorSwatch, &ColorSwatchButton::clicked, this, [this]() {
        openColorFlyout(m_textColorSwatch, m_textColorHexEdit, InspectorField::TextColorHex);
    });

    m_strokeColorField = new Field();
    m_strokeColorField->setLabelText(tr("描边颜色"));
    auto* strokeColorRow = new QWidget();
    auto* strokeColorLayout = new QHBoxLayout(strokeColorRow);
    strokeColorLayout->setContentsMargins(0, 0, 0, 0);
    strokeColorLayout->setSpacing(8);
    m_strokeColorSwatch = new ColorSwatchButton();
    m_strokeColorSwatch->setObjectName(QStringLiteral("inspectorStrokeColorSwatch"));
    m_strokeColorSwatch->setAccessibleName(tr("选择描边颜色"));
    m_strokeColorHexEdit = new LineEdit();
    m_strokeColorHexEdit->setObjectName(QStringLiteral("inspectorStrokeColorHex"));
    m_strokeColorHexEdit->setPlaceholderText(QStringLiteral("#RRGGBB"));
    m_strokeColorHexEdit->setAccessibleName(tr("描边颜色十六进制值"));
    strokeColorLayout->addWidget(m_strokeColorSwatch);
    strokeColorLayout->addWidget(m_strokeColorHexEdit, 1);
    m_strokeColorField->setEditor(strokeColorRow, fluent::WidgetOwnership::Owned);
    typesetLayout->addWidget(m_strokeColorField);
    connect(m_strokeColorHexEdit, &LineEdit::editingFinished, this, [this]() {
        if (!m_updating) {
            handleHexCommit(
                m_strokeColorHexEdit, m_strokeColorField, m_strokeColorSwatch, InspectorField::StrokeColorHex);
        }
    });
    connect(m_strokeColorSwatch, &ColorSwatchButton::clicked, this, [this]() {
        openColorFlyout(m_strokeColorSwatch, m_strokeColorHexEdit, InspectorField::StrokeColorHex);
    });

    auto [strokeWidthField, strokeWidthBox] = makeNumericRow(tr("描边宽度 (px)"), 0.0, 32.0, 1, typesetLayout);
    m_strokeWidthField = strokeWidthField;
    m_strokeWidthBox = strokeWidthBox;
    connect(m_strokeWidthBox, &NumberBox::valueChanged, this, [this](double value) {
        if (!m_updating) {
            handleNumericCommit(m_strokeWidthField, InspectorField::StrokeWidthPx, value);
        }
    });

    m_typesettingExpander->setContentWidget(typesetContent, fluent::WidgetOwnership::Owned);
    m_typesettingExpander->setExpanded(true);
    layout->addWidget(m_typesettingExpander);

    // Region section (collapsed by default). Geometry is displayed read-only; the
    // canvas owns geometry commits in this version.
    m_regionExpander = new Expander(host);
    m_regionExpander->setHeaderText(tr("区域"));
    auto* regionContent = new QWidget();
    auto* regionLayout = new QVBoxLayout(regionContent);
    regionLayout->setContentsMargins(12, 8, 12, 12);
    regionLayout->setSpacing(8);

    auto* geometryRow = new QWidget();
    auto* geometryGrid = new QHBoxLayout(geometryRow);
    geometryGrid->setContentsMargins(0, 0, 0, 0);
    geometryGrid->setSpacing(8);
    const char* geometryLabels[4] = {"X", "Y", "W", "H"};
    fluent::textfields::NumberBox* geometryBoxes[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i) {
        auto* field = new Field();
        field->setLabelText(QString::fromLatin1(geometryLabels[i]));
        auto* box = new NumberBox();
        box->setRange(0.0, 99999.0);
        box->setDisplayPrecision(1);
        box->setSpinButtonPlacementMode(NumberBox::SpinButtonPlacementMode::Hidden);
        box->setReadOnly(true);
        field->setEditor(box, fluent::WidgetOwnership::Owned);
        geometryGrid->addWidget(field);
        geometryBoxes[i] = box;
    }
    m_regionXBox = geometryBoxes[0];
    m_regionYBox = geometryBoxes[1];
    m_regionWidthBox = geometryBoxes[2];
    m_regionHeightBox = geometryBoxes[3];
    regionLayout->addWidget(geometryRow);

    m_regionOrderLabel = new Label();
    m_regionOrderLabel->setObjectName(QStringLiteral("inspectorRegionOrder"));
    m_regionOrderLabel->setTextColorRole(Label::TextColorRole::Secondary);
    regionLayout->addWidget(m_regionOrderLabel);

    m_regionExpander->setContentWidget(regionContent, fluent::WidgetOwnership::Owned);
    layout->addWidget(m_regionExpander);

    layout->addStretch(1);
    return host;
}

QWidget* InspectorPanel::buildMultiHost() {
    using fluent::basicinput::Button;
    using fluent::basicinput::ComboBox;
    using fluent::basicinput::ToggleSwitch;
    using fluent::layout::Field;
    using fluent::textfields::Label;
    using fluent::textfields::LineEdit;
    using fluent::textfields::NumberBox;

    auto* host = new QWidget();
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(12);

    m_multiHintLabel = new Label(host);
    m_multiHintLabel->setObjectName(QStringLiteral("inspectorMultiHint"));
    m_multiHintLabel->setTextColorRole(Label::TextColorRole::Secondary);
    m_multiHintLabel->setWordWrap(true);
    layout->addWidget(m_multiHintLabel);

    auto* multiActionsRow = new QWidget();
    auto* multiActionsLayout = new QHBoxLayout(multiActionsRow);
    multiActionsLayout->setContentsMargins(0, 4, 0, 4);
    multiActionsLayout->setSpacing(8);
    m_multiOcrButton = new Button(tr("批量OCR"));
    m_multiOcrButton->setObjectName(QStringLiteral("inspectorMultiOcr"));
    m_multiTranslateButton = new Button(tr("批量翻译"));
    m_multiTranslateButton->setObjectName(QStringLiteral("inspectorMultiTranslate"));
    m_multiClearButton = new Button(tr("批量涂抹"));
    m_multiClearButton->setObjectName(QStringLiteral("inspectorMultiClear"));
    m_multiLayoutButton = new Button(tr("批量排版"));
    m_multiLayoutButton->setObjectName(QStringLiteral("inspectorMultiLayout"));
    multiActionsLayout->addWidget(m_multiOcrButton);
    multiActionsLayout->addWidget(m_multiTranslateButton);
    multiActionsLayout->addWidget(m_multiClearButton);
    multiActionsLayout->addWidget(m_multiLayoutButton);
    layout->addWidget(multiActionsRow);

    connect(m_multiOcrButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT ocrRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_multiTranslateButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT translateRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_multiClearButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT clearRequested(m_currentPageId, m_currentRegionIds);
        }
    });
    connect(m_multiLayoutButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT layoutRequested(m_currentPageId, m_currentRegionIds);
        }
    });

    auto* fontFamilyField = new Field();
    fontFamilyField->setLabelText(tr("字体"));
    m_multiFontFamilyCombo = new ComboBox();
    m_multiFontFamilyCombo->setObjectName(QStringLiteral("inspectorMultiFontFamily"));
    m_multiFontFamilyCombo->setEditable(true);
    m_multiFontFamilyCombo->addItems({QStringLiteral("Sans-Serif"),
                                      QStringLiteral("Serif"),
                                      QStringLiteral("Monospace"),
                                      QStringLiteral("Microsoft YaHei"),
                                      QStringLiteral("SimSun"),
                                      QStringLiteral("KaiTi")});
    fontFamilyField->setEditor(m_multiFontFamilyCombo, fluent::WidgetOwnership::Owned);
    layout->addWidget(fontFamilyField);
    connect(m_multiFontFamilyCombo, &ComboBox::activated, this, [this](int) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::FontFamily, m_multiFontFamilyCombo->currentText());
        }
    });

    auto* fontWeightField = new Field();
    fontWeightField->setLabelText(tr("字重"));
    m_multiFontWeightCombo = new ComboBox();
    m_multiFontWeightCombo->setObjectName(QStringLiteral("inspectorMultiFontWeight"));
    for (int weight = 100; weight <= 900; weight += 100) {
        m_multiFontWeightCombo->addItem(QString::number(weight));
    }
    fontWeightField->setEditor(m_multiFontWeightCombo, fluent::WidgetOwnership::Owned);
    layout->addWidget(fontWeightField);
    connect(m_multiFontWeightCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::FontWeight, (index + 1) * 100);
        }
    });

    auto [fontSizeField, fontSizeBox] = makeNumericRow(tr("字号"), 6.0, 72.0, 1, layout);
    m_multiFontSizeField = fontSizeField;
    m_multiFontSizeBox = fontSizeBox;
    m_multiFontSizeField->setObjectName(QStringLiteral("inspectorMultiFontSizeField"));
    m_multiFontSizeBox->setObjectName(QStringLiteral("inspectorMultiFontSize"));
    connect(m_multiFontSizeBox, &NumberBox::valueChanged, this, [this](double value) {
        if (!m_updating) {
            handleNumericCommit(m_multiFontSizeField, InspectorField::FontSizePt, value);
        }
    });

    auto* orientationField = new Field();
    orientationField->setLabelText(tr("方向"));
    m_multiOrientationCombo = new ComboBox();
    m_multiOrientationCombo->setObjectName(QStringLiteral("inspectorMultiOrientation"));
    m_multiOrientationCombo->addItems({tr("横排"), tr("竖排")});
    orientationField->setEditor(m_multiOrientationCombo, fluent::WidgetOwnership::Owned);
    layout->addWidget(orientationField);
    connect(m_multiOrientationCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::Orientation, index);
        }
    });

    auto* hAlignField = new Field();
    hAlignField->setLabelText(tr("水平对齐"));
    m_multiHAlignCombo = new ComboBox();
    m_multiHAlignCombo->addItems({tr("左对齐"), tr("居中对齐"), tr("右对齐"), tr("两端对齐")});
    hAlignField->setEditor(m_multiHAlignCombo, fluent::WidgetOwnership::Owned);
    layout->addWidget(hAlignField);
    connect(m_multiHAlignCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::HorizontalAlignment, index);
        }
    });

    auto* vAlignField = new Field();
    vAlignField->setLabelText(tr("垂直对齐"));
    m_multiVAlignCombo = new ComboBox();
    m_multiVAlignCombo->addItems({tr("顶部对齐"), tr("垂直居中"), tr("底部对齐")});
    vAlignField->setEditor(m_multiVAlignCombo, fluent::WidgetOwnership::Owned);
    layout->addWidget(vAlignField);
    connect(m_multiVAlignCombo, &ComboBox::activated, this, [this](int index) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::VerticalAlignment, index);
        }
    });

    m_multiTextColorField = new Field();
    m_multiTextColorField->setLabelText(tr("文字颜色"));
    auto* textColorRow = new QWidget();
    auto* textColorLayout = new QHBoxLayout(textColorRow);
    textColorLayout->setContentsMargins(0, 0, 0, 0);
    textColorLayout->setSpacing(8);
    m_multiTextColorSwatch = new ColorSwatchButton();
    m_multiTextColorSwatch->setAccessibleName(tr("选择文字颜色"));
    m_multiTextColorHexEdit = new LineEdit();
    m_multiTextColorHexEdit->setObjectName(QStringLiteral("inspectorMultiTextColorHex"));
    m_multiTextColorHexEdit->setAccessibleName(tr("文字颜色十六进制值"));
    textColorLayout->addWidget(m_multiTextColorSwatch);
    textColorLayout->addWidget(m_multiTextColorHexEdit, 1);
    m_multiTextColorField->setEditor(textColorRow, fluent::WidgetOwnership::Owned);
    layout->addWidget(m_multiTextColorField);
    connect(m_multiTextColorHexEdit, &LineEdit::editingFinished, this, [this]() {
        if (!m_updating) {
            handleHexCommit(
                m_multiTextColorHexEdit, m_multiTextColorField, m_multiTextColorSwatch, InspectorField::TextColorHex);
        }
    });
    connect(m_multiTextColorSwatch, &ColorSwatchButton::clicked, this, [this]() {
        openColorFlyout(m_multiTextColorSwatch, m_multiTextColorHexEdit, InspectorField::TextColorHex);
    });

    m_multiStrokeColorField = new Field();
    m_multiStrokeColorField->setLabelText(tr("描边颜色"));
    auto* strokeColorRow = new QWidget();
    auto* strokeColorLayout = new QHBoxLayout(strokeColorRow);
    strokeColorLayout->setContentsMargins(0, 0, 0, 0);
    strokeColorLayout->setSpacing(8);
    m_multiStrokeColorSwatch = new ColorSwatchButton();
    m_multiStrokeColorSwatch->setAccessibleName(tr("选择描边颜色"));
    m_multiStrokeColorHexEdit = new LineEdit();
    m_multiStrokeColorHexEdit->setObjectName(QStringLiteral("inspectorMultiStrokeColorHex"));
    m_multiStrokeColorHexEdit->setAccessibleName(tr("描边颜色十六进制值"));
    strokeColorLayout->addWidget(m_multiStrokeColorSwatch);
    strokeColorLayout->addWidget(m_multiStrokeColorHexEdit, 1);
    m_multiStrokeColorField->setEditor(strokeColorRow, fluent::WidgetOwnership::Owned);
    layout->addWidget(m_multiStrokeColorField);
    connect(m_multiStrokeColorHexEdit, &LineEdit::editingFinished, this, [this]() {
        if (!m_updating) {
            handleHexCommit(m_multiStrokeColorHexEdit,
                            m_multiStrokeColorField,
                            m_multiStrokeColorSwatch,
                            InspectorField::StrokeColorHex);
        }
    });
    connect(m_multiStrokeColorSwatch, &ColorSwatchButton::clicked, this, [this]() {
        openColorFlyout(m_multiStrokeColorSwatch, m_multiStrokeColorHexEdit, InspectorField::StrokeColorHex);
    });

    m_multiClearedField = new Field();
    m_multiClearedField->setLabelText(tr("标记已清除"));
    m_multiClearedSwitch = new ToggleSwitch();
    m_multiClearedSwitch->setObjectName(QStringLiteral("inspectorMultiCleared"));
    m_multiClearedSwitch->setOnContent(tr("已清除"));
    m_multiClearedSwitch->setOffContent(tr("未清除"));
    m_multiClearedField->setEditor(m_multiClearedSwitch, fluent::WidgetOwnership::Owned);
    layout->addWidget(m_multiClearedField);
    connect(m_multiClearedSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_updating) {
            emitFieldEdit(InspectorField::Cleared, on);
        }
    });

    m_multiDeleteButton = new Button(tr("删除所选区域"), host);
    m_multiDeleteButton->setObjectName(QStringLiteral("inspectorMultiDelete"));
    m_multiDeleteButton->setCriticalOnHover(true);
    layout->addWidget(m_multiDeleteButton);
    connect(m_multiDeleteButton, &Button::clicked, this, [this]() {
        if (!m_currentRegionIds.isEmpty()) {
            Q_EMIT deleteRegionsRequested(m_currentPageId, m_currentRegionIds);
        }
    });

    layout->addStretch(1);
    return host;
}

void InspectorPanel::setInspectorState(const screen::editor::InspectorState& state) {
    m_updating = true;

    if (const auto* page = std::get_if<screen::editor::PageInspectorState>(&state)) {
        m_mode = screen::editor::InspectorMode::Page;
        m_currentPageId = domain::PageId{};
        m_currentRegionIds.clear();
        m_stack->setCurrentWidget(m_pageHost);
        populatePage(*page);
    } else if (const auto* single = std::get_if<screen::editor::SingleRegionInspectorState>(&state)) {
        m_mode = screen::editor::InspectorMode::Single;
        m_stack->setCurrentWidget(m_singleHost);
        populateSingle(*single);
    } else {
        const auto& multi = std::get<screen::editor::MultiRegionInspectorState>(state);
        m_mode = screen::editor::InspectorMode::Multi;
        m_stack->setCurrentWidget(m_multiHost);
        populateMulti(multi);
    }

    m_updating = false;
}

void InspectorPanel::populatePage(const screen::editor::PageInspectorState& state) {
    m_headerTitle->setText(tr("页面"));
    m_headerSubtitle->clear();

    if (state.pageNumber <= 0) {
        m_pageSummaryLabel->setText(tr("未打开项目"));
        m_pageGuidanceLabel->setText(tr("打开项目后，此处显示当前页面的摘要信息。"));
        m_pageShortcutsLabel->clear();
        return;
    }

    const QString direction =
        state.readingDirection == domain::ReadingDirection::RightToLeft ? tr("从右到左") : tr("从左到右");
    m_pageSummaryLabel->setText(
        tr("第 %1 / %2 页\n分辨率：%3 × %4\n阅读方向：%5\n文本区域：%6\nOCR 完成：%7\n翻译完成：%8")
            .arg(state.pageNumber)
            .arg(state.pageCount)
            .arg(state.imageSize.width)
            .arg(state.imageSize.height)
            .arg(direction)
            .arg(state.regionCount)
            .arg(state.ocrCompletedCount)
            .arg(state.translatedCount));
    m_pageGuidanceLabel->setText(tr("选择画布中的文本区域以编辑其文本与排版。"));
    m_pageShortcutsLabel->setText(tr("Ctrl+A 全选区域 · Delete 删除所选"));
}

void InspectorPanel::populateSingle(const screen::editor::SingleRegionInspectorState& state) {
    m_headerTitle->setText(tr("文本区域"));
    const QString regionIdText = QString::fromStdString(state.regionId.toString());
    m_headerSubtitle->setText(regionIdText);
    m_headerSubtitle->setToolTip(regionIdText);

    m_currentPageId = state.pageId;
    m_currentRegionIds = {state.regionId};

    setTextPreservingCursor(m_sourceEdit, state.sourceText);
    switch (state.sourceType) {
        case domain::TextSourceType::OcrAuto:
            m_sourceMetaLabel->setText(tr("OCR 识别 · 置信度 %1").arg(percentText(state.sourceConfidence)));
            break;
        case domain::TextSourceType::OcrManualModified:
            m_sourceMetaLabel->setText(tr("OCR 识别（已人工修改）"));
            break;
        case domain::TextSourceType::PureManual:
            m_sourceMetaLabel->setText(tr("人工输入"));
            break;
    }

    setTextPreservingCursor(m_translationEdit, state.manualTranslation);
    if (state.hasManualTranslation) {
        m_translationMetaLabel->setText(tr("生效译文：人工译文"));
    } else if (!state.effectiveTranslation.isEmpty()) {
        m_translationMetaLabel->setText(tr("生效译文：%1").arg(state.effectiveTranslation));
    } else {
        m_translationMetaLabel->setText(tr("尚无译文"));
    }

    m_candidatesList->clear();
    for (const auto& candidate : state.candidates) {
        auto* item = new QListWidgetItem(
            tr("%1 — %2 · %3").arg(candidate.text, candidate.provider, percentText(candidate.confidence)),
            m_candidatesList);
        item->setData(Qt::UserRole, candidate.id);
        item->setToolTip(candidate.text);
    }
    for (int row = 0; row < state.candidates.size(); ++row) {
        if (state.candidates.at(row).isSelected) {
            m_candidatesList->setCurrentRow(row);
            break;
        }
    }
    m_candidatesExpander->setHeaderText(tr("候选译文 (%1)").arg(state.candidates.size()));
    m_adoptCandidateButton->setEnabled(m_candidatesList->currentRow() >= 0);
    m_clearStatusValue->setText(state.isCleared ? tr("已清除") : tr("未清除"));

    if (m_fontFamilyCombo->findText(state.fontFamily) < 0 && !state.fontFamily.isEmpty()) {
        m_fontFamilyCombo->addItem(state.fontFamily);
    }
    m_fontFamilyCombo->setCurrentText(state.fontFamily);
    m_fontWeightCombo->setCurrentIndex(qBound(0, state.fontWeight / 100 - 1, 8));
    m_fontSizeBox->setValue(state.fontSizePt);
    clearFieldError(m_fontSizeField);
    m_letterSpacingBox->setValue(state.letterSpacingEm);
    clearFieldError(m_letterSpacingField);
    m_orientationCombo->setCurrentIndex(static_cast<int>(state.orientation));
    m_hAlignCombo->setCurrentIndex(static_cast<int>(state.horizontalAlignment));
    m_vAlignCombo->setCurrentIndex(static_cast<int>(state.verticalAlignment));
    m_lineSpacingBox->setValue(state.lineSpacing);
    clearFieldError(m_lineSpacingField);
    m_marginTopBox->setValue(state.margins.top);
    m_marginRightBox->setValue(state.margins.right);
    m_marginBottomBox->setValue(state.margins.bottom);
    m_marginLeftBox->setValue(state.margins.left);
    clearFieldError(m_marginTopField);
    clearFieldError(m_marginRightField);
    clearFieldError(m_marginBottomField);
    clearFieldError(m_marginLeftField);

    m_textColorHexEdit->setText(state.textColorHex);
    m_textColorSwatch->setSwatchColor(QColor(state.textColorHex));
    m_textColorSwatch->setToolTip(state.textColorHex);
    clearFieldError(m_textColorField);
    m_strokeColorHexEdit->setText(state.strokeColorHex);
    m_strokeColorSwatch->setSwatchColor(QColor(state.strokeColorHex));
    m_strokeColorSwatch->setToolTip(state.strokeColorHex);
    clearFieldError(m_strokeColorField);
    m_strokeWidthBox->setValue(state.strokeWidthPx);
    clearFieldError(m_strokeWidthField);

    m_regionXBox->setValue(state.geometry.x);
    m_regionYBox->setValue(state.geometry.y);
    m_regionWidthBox->setValue(state.geometry.width);
    m_regionHeightBox->setValue(state.geometry.height);
    m_regionOrderLabel->setText(tr("阅读顺序：%1").arg(state.orderIndex + 1));

    setLockedEditing(state.isLocked);
}

void InspectorPanel::populateMulti(const screen::editor::MultiRegionInspectorState& state) {
    m_headerTitle->setText(tr("文本区域"));
    m_headerSubtitle->setText(tr("已选择 %1 个区域").arg(state.regionIds.size()));

    m_currentPageId = state.pageId;
    m_currentRegionIds = state.regionIds;

    m_multiHintLabel->setText(tr("批量编辑仅显示可安全修改的共有属性；值不一致时显示“混合”。"));

    const QString mixed = tr("混合");

    if (state.fontFamily.has_value()) {
        if (m_multiFontFamilyCombo->findText(*state.fontFamily) < 0 && !state.fontFamily->isEmpty()) {
            m_multiFontFamilyCombo->addItem(*state.fontFamily);
        }
        m_multiFontFamilyCombo->setCurrentText(*state.fontFamily);
    } else {
        m_multiFontFamilyCombo->setCurrentText(QString());
        if (auto* edit = m_multiFontFamilyCombo->lineEdit()) {
            edit->setPlaceholderText(mixed);
        }
    }

    if (state.fontWeight.has_value()) {
        m_multiFontWeightCombo->setCurrentIndex(qBound(0, *state.fontWeight / 100 - 1, 8));
    } else {
        m_multiFontWeightCombo->setCurrentIndex(-1);
    }

    if (state.fontSizePt.has_value()) {
        m_multiFontSizeBox->setValue(*state.fontSizePt);
        m_multiFontSizeBox->setPlaceholderText(QString());
    } else {
        m_multiFontSizeBox->clear();
        m_multiFontSizeBox->setPlaceholderText(mixed);
    }
    clearFieldError(m_multiFontSizeField);

    const auto setCombo = [](fluent::basicinput::ComboBox* combo, const auto& value) {
        if (value.has_value()) {
            combo->setCurrentIndex(static_cast<int>(*value));
        } else {
            combo->setCurrentIndex(-1);
        }
    };
    setCombo(m_multiOrientationCombo, state.orientation);
    setCombo(m_multiHAlignCombo, state.horizontalAlignment);
    setCombo(m_multiVAlignCombo, state.verticalAlignment);

    const auto setColorRow = [&mixed](ColorSwatchButton* swatch,
                                      fluent::textfields::LineEdit* hexEdit,
                                      fluent::layout::Field* field,
                                      const std::optional<QString>& value) {
        if (value.has_value()) {
            hexEdit->setText(*value);
            hexEdit->setPlaceholderText(QStringLiteral("#RRGGBB"));
            swatch->setSwatchColor(QColor(*value));
            swatch->setToolTip(*value);
        } else {
            hexEdit->clear();
            hexEdit->setPlaceholderText(mixed);
            swatch->setSwatchColor(QColor());
            swatch->setToolTip(mixed);
        }
        clearFieldError(field);
    };
    setColorRow(m_multiTextColorSwatch, m_multiTextColorHexEdit, m_multiTextColorField, state.textColorHex);
    setColorRow(m_multiStrokeColorSwatch, m_multiStrokeColorHexEdit, m_multiStrokeColorField, state.strokeColorHex);

    m_multiClearedSwitch->setIsOn(state.isCleared.value_or(false));
    if (state.isCleared.has_value()) {
        m_multiClearedField->setHelperText(QString());
    } else {
        m_multiClearedField->setHelperText(tr("所选区域的清除状态不一致"));
    }

    const bool editable = !state.allLocked;
    m_multiFontFamilyCombo->setEnabled(editable);
    m_multiFontWeightCombo->setEnabled(editable);
    m_multiFontSizeBox->setEnabled(editable);
    m_multiOrientationCombo->setEnabled(editable);
    m_multiHAlignCombo->setEnabled(editable);
    m_multiVAlignCombo->setEnabled(editable);
    m_multiTextColorHexEdit->setEnabled(editable);
    m_multiTextColorSwatch->setEnabled(editable);
    m_multiStrokeColorHexEdit->setEnabled(editable);
    m_multiStrokeColorSwatch->setEnabled(editable);
    m_multiClearedSwitch->setEnabled(editable);
    m_multiDeleteButton->setEnabled(editable);
}

void InspectorPanel::setLockedEditing(bool locked) {
    m_sourceEdit->setReadOnly(locked);
    m_translationEdit->setReadOnly(locked);
    m_candidatesList->setEnabled(!locked);
    m_adoptCandidateButton->setEnabled(!locked && m_candidatesList->currentRow() >= 0);
    m_fontFamilyCombo->setEnabled(!locked);
    m_fontWeightCombo->setEnabled(!locked);
    m_fontSizeBox->setEnabled(!locked);
    m_letterSpacingBox->setEnabled(!locked);
    m_orientationCombo->setEnabled(!locked);
    m_hAlignCombo->setEnabled(!locked);
    m_vAlignCombo->setEnabled(!locked);
    m_lineSpacingBox->setEnabled(!locked);
    m_marginTopBox->setEnabled(!locked);
    m_marginRightBox->setEnabled(!locked);
    m_marginBottomBox->setEnabled(!locked);
    m_marginLeftBox->setEnabled(!locked);
    m_textColorHexEdit->setEnabled(!locked);
    m_textColorSwatch->setEnabled(!locked);
    m_strokeColorHexEdit->setEnabled(!locked);
    m_strokeColorSwatch->setEnabled(!locked);
    m_strokeWidthBox->setEnabled(!locked);
}

void InspectorPanel::emitFieldEdit(screen::editor::InspectorField field, const QVariant& value) {
    if (m_currentRegionIds.isEmpty()) {
        return;
    }
    Q_EMIT fieldEditCommitted(m_currentPageId, m_currentRegionIds, field, value);
}

bool InspectorPanel::eventFilter(QObject* watched, QEvent* event) {
    // The filter is installed only on the two inner text editors; a FocusOut here
    // always means a text control lost focus and the merge batch must be sealed.
    if (event->type() == QEvent::FocusOut) {
        Q_EMIT textEditingSessionFinished();
    } else if (event->type() == QEvent::InputMethod) {
        auto* imeEvent = static_cast<QInputMethodEvent*>(event);
        m_inImePreedit = !imeEvent->preeditString().isEmpty();
    }
    return QWidget::eventFilter(watched, event);
}

void InspectorPanel::handleNumericCommit(fluent::layout::Field* field,
                                         screen::editor::InspectorField inspectorField,
                                         double value) {
    if (std::isnan(value)) {
        // Unparseable input: the NumberBox keeps the text, we only surface the error.
        field->setValidationState(fluent::layout::Field::ValidationState::Error);
        field->setValidationMessage(tr("请输入有效数字"));
        return;
    }
    clearFieldError(field);
    emitFieldEdit(inspectorField, value);
}

void InspectorPanel::handleHexCommit(fluent::textfields::LineEdit* edit,
                                     fluent::layout::Field* field,
                                     ColorSwatchButton* swatch,
                                     screen::editor::InspectorField inspectorField) {
    const QString text = edit->text().trimmed();
    if (!hexColorPattern().match(text).hasMatch()) {
        field->setValidationState(fluent::layout::Field::ValidationState::Error);
        field->setValidationMessage(tr("颜色格式为 #RRGGBB"));
        return;
    }
    clearFieldError(field);
    const QString normalized = text.toUpper();
    swatch->setSwatchColor(QColor(normalized));
    swatch->setToolTip(normalized);
    emitFieldEdit(inspectorField, normalized);
}

void InspectorPanel::openColorFlyout(ColorSwatchButton* swatch,
                                     fluent::textfields::LineEdit* hexEdit,
                                     screen::editor::InspectorField field) {
    auto* flyout = new fluent::dialogs_flyouts::Flyout(this);
    // Flyout is a light-dismiss popup that never self-deletes; destroy it on close.
    // The hosted ColorPicker is a flyout child and dies with it.
    flyout->setAttribute(Qt::WA_DeleteOnClose);
    auto* picker = new fluent::basicinput::ColorPicker(flyout);
    if (swatch->swatchColor().isValid()) {
        picker->setColor(swatch->swatchColor());
    }
    picker->setAlphaEnabled(false);
    auto* flyoutLayout = new QVBoxLayout(flyout);
    flyoutLayout->addWidget(picker);
    connect(picker,
            &fluent::basicinput::ColorPicker::colorChanged,
            this,
            [this, swatch, hexEdit, field](const QColor& color) {
                if (!color.isValid()) {
                    return;
                }
                const QString hex = color.name(QColor::HexRgb).toUpper();
                hexEdit->setText(hex);
                swatch->setSwatchColor(color);
                swatch->setToolTip(hex);
                emitFieldEdit(field, hex);
            });
    flyout->showAt(swatch);
}

void InspectorPanel::onThemeUpdated() {
    const auto& colors = themeColorsRef();
    for (auto* swatch : {m_textColorSwatch, m_strokeColorSwatch, m_multiTextColorSwatch, m_multiStrokeColorSwatch}) {
        if (swatch != nullptr) {
            swatch->setBorderColor(colors.strokeSecondary);
        }
    }
    if (m_candidatesList != nullptr) {
        QPalette palette = m_candidatesList->palette();
        palette.setColor(QPalette::Base, colors.bgLayer);
        palette.setColor(QPalette::Text, colors.textPrimary);
        palette.setColor(QPalette::Highlight, colors.accentDefault);
        palette.setColor(QPalette::HighlightedText, colors.textOnAccent);
        m_candidatesList->setPalette(palette);
    }
}

} // namespace manga::ui::widget
