#include "ui/widget/InspectorPanel.h"

#include "ui/widget/TransparentScrollView.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Layout.h>
#include <FluentQt/Scrolling.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <array>
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
    setInspectorState(std::monostate{});
}

void InspectorPanel::setOcrAvailability(bool available, const QString& unavailableReason) {
    m_ocrAvailable = available;
    m_ocrUnavailableReason = available ? QString() : unavailableReason;
}

void InspectorPanel::setTranslationAvailability(bool available, const QString& unavailableReason) {
    m_translationAvailable = available;
    m_translationUnavailableReason = available ? QString() : unavailableReason;
    if (m_singleTranslateButton != nullptr) {
        m_singleTranslateButton->setEnabled(available && m_mode == screen::editor::InspectorMode::Single);
        m_singleTranslateButton->setToolTip(available ? tr("使用当前 AI 服务重新翻译") : tr("请先配置 AI 翻译服务"));
    }
    updateTranslationInfoBar();
}

void InspectorPanel::setProcessingState(const screen::editor::EditorProcessingState& processingState) {
    m_processingState = processingState;
    updateTranslationInfoBar();
}

void InspectorPanel::updateTranslationInfoBar() {
    if (m_translationInfoBar == nullptr) {
        return;
    }

    if (!m_translationAvailable) {
        m_translationInfoBar->setSeverity(fluent::status_info::InfoBar::Warning);
        m_translationInfoBar->setTitle(tr("AI 翻译未配置"));
        m_translationInfoBar->setMessage(m_translationUnavailableReason.isEmpty()
                                             ? tr("请在设置中配置 API 端点、模型和密钥。")
                                             : m_translationUnavailableReason);
        m_translationInfoBar->setActionWidget(m_translationSettingsButton);
        m_translationSettingsButton->show();
        m_translationInfoBar->setIsOpen(true);
        return;
    }

    if (!m_processingState.safeMessage.isEmpty() &&
        (m_processingState.status == screen::editor::ProcessingStatus::Failed ||
         m_processingState.status == screen::editor::ProcessingStatus::NeedsReview)) {
        const bool failed = m_processingState.status == screen::editor::ProcessingStatus::Failed;
        m_translationInfoBar->setSeverity(failed ? fluent::status_info::InfoBar::Error
                                                 : fluent::status_info::InfoBar::Warning);
        m_translationInfoBar->setTitle(failed ? tr("翻译失败") : tr("翻译需要处理"));
        m_translationInfoBar->setMessage(m_processingState.safeMessage);
        m_translationInfoBar->setActionWidget(nullptr);
        m_translationSettingsButton->hide();
        m_translationInfoBar->setIsOpen(true);
        return;
    }

    m_translationInfoBar->setActionWidget(nullptr);
    m_translationSettingsButton->hide();
    m_translationInfoBar->setIsOpen(false);
}

void InspectorPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    syncContentWidth();
}

void InspectorPanel::syncContentWidth() {
    if (m_scrollView == nullptr || m_contentHost == nullptr || m_scrollView->viewport() == nullptr) {
        return;
    }

    const int contentWidth = m_scrollView->viewport()->width();
    m_contentHost->setMinimumWidth(contentWidth);
    m_contentHost->setMaximumWidth(contentWidth);
}

void InspectorPanel::buildUi() {
    using fluent::layout::Field;
    using fluent::textfields::Label;

    setMinimumWidth(kPanelMinimumWidth);
    setFocusPolicy(Qt::StrongFocus);

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

    m_scrollView = new TransparentScrollView(this);
    m_scrollView->setWidgetResizable(true);
    m_scrollView->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
    m_scrollView->viewport()->installEventFilter(this);
    root->addWidget(m_scrollView, 1);

    m_contentHost = buildContentHost();
    m_contentHost->setMinimumWidth(0);
    m_contentHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_scrollView->setContentWidget(m_contentHost, fluent::WidgetOwnership::Owned);
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

QWidget* InspectorPanel::buildContentHost() {
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

    m_translationInfoBar = new fluent::status_info::InfoBar(translationContent);
    m_translationInfoBar->setObjectName(QStringLiteral("inspectorTranslationInfoBar"));
    m_translationInfoBar->setIsClosable(false);
    m_translationInfoBar->setIsOpen(false);
    m_translationSettingsButton = new Button(tr("打开设置"));
    m_translationSettingsButton->setObjectName(QStringLiteral("inspectorTranslationSettings"));
    m_translationSettingsButton->setAccessibleName(tr("打开 AI 翻译设置"));
    connect(m_translationSettingsButton, &Button::clicked, this, &InspectorPanel::translationSettingsRequested);
    translationLayout->addWidget(m_translationInfoBar);

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
    m_candidatesExpander->setObjectName(QStringLiteral("inspectorCandidatesExpander"));
    m_candidatesExpander->setHeaderText(tr("候选译文"));
    auto* candidatesContent = new QWidget();
    auto* candidatesLayout = new QVBoxLayout(candidatesContent);
    candidatesLayout->setContentsMargins(12, 8, 12, 12);
    candidatesLayout->setSpacing(8);
    m_candidatesList = new QListWidget();
    m_candidatesList->setObjectName(QStringLiteral("inspectorCandidatesList"));
    m_candidatesList->setAccessibleName(tr("候选译文列表"));
    m_candidatesList->setMaximumHeight(160);
    candidatesLayout->addWidget(m_candidatesList);
    m_adoptCandidateButton = new Button(tr("采用候选"));
    m_adoptCandidateButton->setObjectName(QStringLiteral("inspectorAdoptCandidate"));
    m_adoptCandidateButton->setToolTip(tr("采用选中的候选译文"));
    m_adoptCandidateButton->setAccessibleName(tr("采用候选译文"));
    m_adoptCandidateButton->setEnabled(false);
    candidatesLayout->addWidget(m_adoptCandidateButton);
    m_candidatesExpander->setContentWidget(candidatesContent, fluent::WidgetOwnership::Owned);
    translationLayout->addWidget(m_candidatesExpander);

    auto* singleActionsRow = new QWidget();
    auto* singleActionsLayout = new QHBoxLayout(singleActionsRow);
    singleActionsLayout->setContentsMargins(0, 4, 0, 4);
    singleActionsLayout->setSpacing(8);
    singleActionsLayout->addStretch(1);
    m_singleTranslateButton = new Button(tr("AI 重译"));
    m_singleTranslateButton->setObjectName(QStringLiteral("inspectorSingleTranslate"));
    m_singleTranslateButton->setFluentStyle(Button::Accent);
    m_singleTranslateButton->setToolTip(tr("使用当前 AI 服务重新翻译"));
    m_singleTranslateButton->setAccessibleName(tr("AI 重译"));
    singleActionsLayout->addWidget(m_singleTranslateButton);
    translationLayout->addWidget(singleActionsRow);

    connect(m_singleTranslateButton, &Button::clicked, this, [this]() {
        if (m_currentPageId.isValid() && !m_currentRegionIds.isEmpty()) {
            Q_EMIT translateRequested(m_currentPageId, m_currentRegionIds);
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

    layout->addStretch(1);
    return host;
}

void InspectorPanel::focusTranslationEditor() {
    if (m_translationEdit != nullptr && m_translationEdit->isVisible() && m_translationEdit->isEnabled()) {
        m_translationEdit->setFocus(Qt::OtherFocusReason);
    }
}

void InspectorPanel::setInspectorState(const screen::editor::InspectorState& state) {
    m_updating = true;

    if (std::holds_alternative<screen::editor::SingleRegionInspectorState>(state)) {
        m_mode = screen::editor::InspectorMode::Single;
        m_contentHost->setEnabled(true);
        m_translationExpander->setVisible(true);
        populateSingle(std::get<screen::editor::SingleRegionInspectorState>(state));
    } else if (std::holds_alternative<screen::editor::MultiRegionInspectorState>(state)) {
        m_mode = screen::editor::InspectorMode::Multi;
        m_contentHost->setEnabled(true);
        m_translationExpander->setVisible(false);
        populateMulti(std::get<screen::editor::MultiRegionInspectorState>(state));
    } else {
        m_mode = screen::editor::InspectorMode::None;
        m_translationExpander->setVisible(true);
        populateNone();
    }

    m_updating = false;
}

void InspectorPanel::populateNone() {
    m_currentPageId = domain::PageId{};
    m_currentRegionIds.clear();

    m_headerTitle->setText(tr("属性"));
    m_headerSubtitle->setText(tr("未选中文字框"));
    m_headerSubtitle->setToolTip(QString());

    m_contentHost->setEnabled(false);

    // Reset fields to placeholders
    setTextPreservingCursor(m_sourceEdit, QString());
    m_sourceMetaLabel->clear();
    setTextPreservingCursor(m_translationEdit, QString());
    m_translationMetaLabel->clear();
    m_candidatesList->clear();
    m_candidatesExpander->setVisible(false);
    m_adoptCandidateButton->setEnabled(false);

    m_fontSizeBox->setValue(14.0);
    clearFieldError(m_fontSizeField);
    m_letterSpacingBox->setValue(0.0);
    clearFieldError(m_letterSpacingField);
    m_lineSpacingBox->setValue(1.2);
    clearFieldError(m_lineSpacingField);
    m_marginTopBox->setValue(0.0);
    m_marginRightBox->setValue(0.0);
    m_marginBottomBox->setValue(0.0);
    m_marginLeftBox->setValue(0.0);
    clearFieldError(m_marginTopField);
    clearFieldError(m_marginRightField);
    clearFieldError(m_marginBottomField);
    clearFieldError(m_marginLeftField);

    m_textColorHexEdit->clear();
    m_textColorSwatch->setSwatchColor(QColor());
    m_textColorSwatch->setToolTip(QString());
    clearFieldError(m_textColorField);

    m_strokeColorHexEdit->clear();
    m_strokeColorSwatch->setSwatchColor(QColor());
    m_strokeColorSwatch->setToolTip(QString());
    clearFieldError(m_strokeColorField);

    m_strokeWidthBox->setValue(0.0);
    clearFieldError(m_strokeWidthField);
}

void InspectorPanel::populateMulti(const screen::editor::MultiRegionInspectorState& state) {
    m_headerTitle->setText(tr("文字框属性"));
    m_headerSubtitle->setText(tr("已选中 %1 个文字框").arg(state.regionCount));
    m_headerSubtitle->setToolTip(QString());

    m_currentPageId = state.pageId;
    m_currentRegionIds = state.regionIds;

    // Reset translation fields while hidden
    setTextPreservingCursor(m_sourceEdit, QString());
    m_sourceMetaLabel->clear();
    setTextPreservingCursor(m_translationEdit, QString());
    m_translationMetaLabel->clear();
    m_candidatesList->clear();
    m_candidatesExpander->setVisible(false);
    m_adoptCandidateButton->setEnabled(false);

    // Font Family
    if (state.fontFamily.has_value()) {
        if (m_fontFamilyCombo->findText(*state.fontFamily) < 0 && !state.fontFamily->isEmpty()) {
            m_fontFamilyCombo->addItem(*state.fontFamily);
        }
        m_fontFamilyCombo->setCurrentText(*state.fontFamily);
    } else {
        m_fontFamilyCombo->setCurrentIndex(-1);
        m_fontFamilyCombo->setEditText(QString());
    }

    // Font Weight
    if (state.fontWeight.has_value()) {
        m_fontWeightCombo->setCurrentIndex(qBound(0, *state.fontWeight / 100 - 1, 8));
    } else {
        m_fontWeightCombo->setCurrentIndex(-1);
    }

    // Font Size
    if (state.fontSizePt.has_value()) {
        m_fontSizeBox->setValue(*state.fontSizePt);
    } else {
        m_fontSizeBox->clear();
    }
    clearFieldError(m_fontSizeField);

    // Letter Spacing
    if (state.letterSpacingEm.has_value()) {
        m_letterSpacingBox->setValue(*state.letterSpacingEm);
    } else {
        m_letterSpacingBox->clear();
    }
    clearFieldError(m_letterSpacingField);

    // Orientation
    if (state.orientation.has_value()) {
        m_orientationCombo->setCurrentIndex(static_cast<int>(*state.orientation));
    } else {
        m_orientationCombo->setCurrentIndex(-1);
    }

    // Horizontal Alignment
    if (state.horizontalAlignment.has_value()) {
        m_hAlignCombo->setCurrentIndex(static_cast<int>(*state.horizontalAlignment));
    } else {
        m_hAlignCombo->setCurrentIndex(-1);
    }

    // Vertical Alignment
    if (state.verticalAlignment.has_value()) {
        m_vAlignCombo->setCurrentIndex(static_cast<int>(*state.verticalAlignment));
    } else {
        m_vAlignCombo->setCurrentIndex(-1);
    }

    // Line Spacing
    if (state.lineSpacing.has_value()) {
        m_lineSpacingBox->setValue(*state.lineSpacing);
    } else {
        m_lineSpacingBox->clear();
    }
    clearFieldError(m_lineSpacingField);

    // Margins
    if (state.marginTop.has_value()) {
        m_marginTopBox->setValue(*state.marginTop);
    } else {
        m_marginTopBox->clear();
    }
    if (state.marginRight.has_value()) {
        m_marginRightBox->setValue(*state.marginRight);
    } else {
        m_marginRightBox->clear();
    }
    if (state.marginBottom.has_value()) {
        m_marginBottomBox->setValue(*state.marginBottom);
    } else {
        m_marginBottomBox->clear();
    }
    if (state.marginLeft.has_value()) {
        m_marginLeftBox->setValue(*state.marginLeft);
    } else {
        m_marginLeftBox->clear();
    }
    clearFieldError(m_marginTopField);
    clearFieldError(m_marginRightField);
    clearFieldError(m_marginBottomField);
    clearFieldError(m_marginLeftField);

    // Text Color
    if (state.textColorHex.has_value()) {
        m_textColorHexEdit->setText(*state.textColorHex);
        m_textColorSwatch->setSwatchColor(QColor(*state.textColorHex));
        m_textColorSwatch->setToolTip(*state.textColorHex);
    } else {
        m_textColorHexEdit->clear();
        m_textColorSwatch->setSwatchColor(QColor());
        m_textColorSwatch->setToolTip(tr("多种颜色"));
    }
    clearFieldError(m_textColorField);

    // Stroke Color
    if (state.strokeColorHex.has_value()) {
        m_strokeColorHexEdit->setText(*state.strokeColorHex);
        m_strokeColorSwatch->setSwatchColor(QColor(*state.strokeColorHex));
        m_strokeColorSwatch->setToolTip(*state.strokeColorHex);
    } else {
        m_strokeColorHexEdit->clear();
        m_strokeColorSwatch->setSwatchColor(QColor());
        m_strokeColorSwatch->setToolTip(tr("多种颜色"));
    }
    clearFieldError(m_strokeColorField);

    // Stroke Width
    if (state.strokeWidthPx.has_value()) {
        m_strokeWidthBox->setValue(*state.strokeWidthPx);
    } else {
        m_strokeWidthBox->clear();
    }
    clearFieldError(m_strokeWidthField);

    setLockedEditing(state.allLocked);
}

void InspectorPanel::populateSingle(const screen::editor::SingleRegionInspectorState& state) {
    m_headerTitle->setText(tr("文字框属性"));
    const QString subtitle = tr("序号 #%1 · %2 × %3")
                                 .arg(state.orderIndex + 1)
                                 .arg(static_cast<int>(state.geometry.width))
                                 .arg(static_cast<int>(state.geometry.height));
    m_headerSubtitle->setText(subtitle);
    m_headerSubtitle->setToolTip(QString::fromStdString(state.regionId.toString()));

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
    m_candidatesExpander->setVisible(!state.candidates.isEmpty());
    m_adoptCandidateButton->setEnabled(m_candidatesList->currentRow() >= 0);

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

    setLockedEditing(state.isLocked);
}

void InspectorPanel::setLockedEditing(bool locked) {
    m_sourceEdit->setReadOnly(locked);
    m_translationEdit->setReadOnly(locked);
    m_singleTranslateButton->setEnabled(!locked && m_translationAvailable);
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
    if (m_scrollView != nullptr && watched == m_scrollView->viewport()) {
        if (event->type() == QEvent::Resize) {
            syncContentWidth();
        }
        return QWidget::eventFilter(watched, event);
    }

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
    for (auto* swatch : {m_textColorSwatch, m_strokeColorSwatch}) {
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
