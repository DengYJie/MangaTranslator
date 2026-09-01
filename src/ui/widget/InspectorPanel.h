#pragma once

#include "domain/model/Uuid.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/InspectorState.h"

#include <FluentQt/Foundation.h>
#include <QWidget>

class QListWidget;
class QStackedWidget;

namespace fluent::basicinput {
class Button;
class ComboBox;
class ToggleSwitch;
} // namespace fluent::basicinput
namespace fluent::layout {
class Expander;
class Field;
} // namespace fluent::layout
namespace fluent::textfields {
class Label;
class LineEdit;
class NumberBox;
class TextEdit;
} // namespace fluent::textfields

namespace manga::ui::widget {

class ColorSwatchButton;
class TransparentScrollView;

/// Selection-driven region inspector. Renders the InspectorState projection through
/// three mutually exclusive hosts (page summary, single region, multi region) and
/// reports user intent through stable PageId/RegionId/field-enum signals only.
class InspectorPanel : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void onThemeUpdated() override;

    /// Renders the inspector projection; switches hosts, never stacks them.
    void setInspectorState(const screen::editor::InspectorState& state);

    /// Mode of the currently visible host.
    [[nodiscard]] screen::editor::InspectorMode modeForTest() const noexcept {
        return m_mode;
    }
    [[nodiscard]] QWidget* pageHostForTest() const {
        return m_pageHost;
    }
    [[nodiscard]] QWidget* singleHostForTest() const {
        return m_singleHost;
    }
    [[nodiscard]] QWidget* multiHostForTest() const {
        return m_multiHost;
    }

Q_SIGNALS:
    /// A committed, locally validated edit. regionIds holds exactly one id in single
    /// mode and the whole batch in multi mode. The QVariant payload type depends on
    /// the field: QString, double, int, or bool.
    void fieldEditCommitted(const domain::PageId& pageId,
                            const QVector<domain::RegionId>& regionIds,
                            screen::editor::InspectorField field,
                            const QVariant& value);
    void candidateAdoptionRequested(const domain::PageId& pageId,
                                    const domain::RegionId& regionId,
                                    const QString& candidateId);
    void deleteRegionsRequested(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void ocrRequested(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void translateRequested(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void clearRequested(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    void layoutRequested(const domain::PageId& pageId, const QVector<domain::RegionId>& regionIds);
    /// A text control lost focus; the ViewModel seals the active text merge batch.
    void textEditingSessionFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    QWidget* buildPageHost();
    QWidget* buildSingleHost();
    QWidget* buildMultiHost();

    void populatePage(const screen::editor::PageInspectorState& state);
    void populateSingle(const screen::editor::SingleRegionInspectorState& state);
    void populateMulti(const screen::editor::MultiRegionInspectorState& state);

    /// Emits fieldEditCommitted for the given ids unless a programmatic update is running.
    void emitFieldEdit(screen::editor::InspectorField field, const QVariant& value);

    /// NumberBox commit: NaN means unparseable input; keeps the text, shows the field
    /// error and skips the intent. Valid values are already clamped by the NumberBox.
    void handleNumericCommit(fluent::layout::Field* field, screen::editor::InspectorField inspectorField, double value);
    /// Hex color commit: keeps invalid text with an error, never clears it.
    void handleHexCommit(fluent::textfields::LineEdit* edit,
                         fluent::layout::Field* field,
                         ColorSwatchButton* swatch,
                         screen::editor::InspectorField inspectorField);
    void openColorFlyout(ColorSwatchButton* swatch,
                         fluent::textfields::LineEdit* hexEdit,
                         screen::editor::InspectorField field);

    void setLockedEditing(bool locked);
    bool m_updating{false};
    bool m_inImePreedit{false};
    screen::editor::InspectorMode m_mode{screen::editor::InspectorMode::Page};
    /// Identity of the rendered projection, used to build intent signals.
    domain::PageId m_currentPageId;
    QVector<domain::RegionId> m_currentRegionIds;

    fluent::textfields::Label* m_headerTitle{nullptr};
    fluent::textfields::Label* m_headerSubtitle{nullptr};
    TransparentScrollView* m_scrollView{nullptr};
    QStackedWidget* m_stack{nullptr};
    QWidget* m_pageHost{nullptr};
    QWidget* m_singleHost{nullptr};
    QWidget* m_multiHost{nullptr};

    // Page host.
    fluent::textfields::Label* m_pageSummaryLabel{nullptr};
    fluent::textfields::Label* m_pageGuidanceLabel{nullptr};
    fluent::textfields::Label* m_pageShortcutsLabel{nullptr};

    // Single host: translation.
    fluent::layout::Expander* m_translationExpander{nullptr};
    fluent::textfields::TextEdit* m_sourceEdit{nullptr};
    fluent::textfields::Label* m_sourceMetaLabel{nullptr};
    fluent::textfields::TextEdit* m_translationEdit{nullptr};
    fluent::textfields::Label* m_translationMetaLabel{nullptr};
    fluent::textfields::Label* m_clearStatusValue{nullptr};
    fluent::layout::Expander* m_candidatesExpander{nullptr};
    QListWidget* m_candidatesList{nullptr};
    fluent::basicinput::Button* m_adoptCandidateButton{nullptr};

    // Single host: typesetting.
    fluent::layout::Expander* m_typesettingExpander{nullptr};
    fluent::basicinput::ComboBox* m_fontFamilyCombo{nullptr};
    fluent::basicinput::ComboBox* m_fontWeightCombo{nullptr};
    fluent::layout::Field* m_fontSizeField{nullptr};
    fluent::textfields::NumberBox* m_fontSizeBox{nullptr};
    fluent::layout::Field* m_letterSpacingField{nullptr};
    fluent::textfields::NumberBox* m_letterSpacingBox{nullptr};
    fluent::basicinput::ComboBox* m_orientationCombo{nullptr};
    fluent::basicinput::ComboBox* m_hAlignCombo{nullptr};
    fluent::basicinput::ComboBox* m_vAlignCombo{nullptr};
    fluent::layout::Field* m_lineSpacingField{nullptr};
    fluent::textfields::NumberBox* m_lineSpacingBox{nullptr};
    fluent::layout::Field* m_marginTopField{nullptr};
    fluent::layout::Field* m_marginRightField{nullptr};
    fluent::layout::Field* m_marginBottomField{nullptr};
    fluent::layout::Field* m_marginLeftField{nullptr};
    fluent::textfields::NumberBox* m_marginTopBox{nullptr};
    fluent::textfields::NumberBox* m_marginRightBox{nullptr};
    fluent::textfields::NumberBox* m_marginBottomBox{nullptr};
    fluent::textfields::NumberBox* m_marginLeftBox{nullptr};
    fluent::layout::Field* m_textColorField{nullptr};
    ColorSwatchButton* m_textColorSwatch{nullptr};
    fluent::textfields::LineEdit* m_textColorHexEdit{nullptr};
    fluent::layout::Field* m_strokeColorField{nullptr};
    ColorSwatchButton* m_strokeColorSwatch{nullptr};
    fluent::textfields::LineEdit* m_strokeColorHexEdit{nullptr};
    fluent::layout::Field* m_strokeWidthField{nullptr};
    fluent::textfields::NumberBox* m_strokeWidthBox{nullptr};

    // Single host: region (read-only display; the canvas owns geometry commits).
    fluent::layout::Expander* m_regionExpander{nullptr};
    fluent::textfields::NumberBox* m_regionXBox{nullptr};
    fluent::textfields::NumberBox* m_regionYBox{nullptr};
    fluent::textfields::NumberBox* m_regionWidthBox{nullptr};
    fluent::textfields::NumberBox* m_regionHeightBox{nullptr};
    fluent::textfields::Label* m_regionOrderLabel{nullptr};

    fluent::basicinput::Button* m_singleOcrButton{nullptr};
    fluent::basicinput::Button* m_singleTranslateButton{nullptr};
    fluent::basicinput::Button* m_singleClearButton{nullptr};
    fluent::basicinput::Button* m_singleLayoutButton{nullptr};

    // Multi host: safe intersection only.
    fluent::textfields::Label* m_multiHintLabel{nullptr};
    fluent::basicinput::Button* m_multiOcrButton{nullptr};
    fluent::basicinput::Button* m_multiTranslateButton{nullptr};
    fluent::basicinput::Button* m_multiClearButton{nullptr};
    fluent::basicinput::Button* m_multiLayoutButton{nullptr};
    fluent::basicinput::ComboBox* m_multiFontFamilyCombo{nullptr};
    fluent::basicinput::ComboBox* m_multiFontWeightCombo{nullptr};
    fluent::layout::Field* m_multiFontSizeField{nullptr};
    fluent::textfields::NumberBox* m_multiFontSizeBox{nullptr};
    fluent::basicinput::ComboBox* m_multiOrientationCombo{nullptr};
    fluent::basicinput::ComboBox* m_multiHAlignCombo{nullptr};
    fluent::basicinput::ComboBox* m_multiVAlignCombo{nullptr};
    fluent::layout::Field* m_multiTextColorField{nullptr};
    ColorSwatchButton* m_multiTextColorSwatch{nullptr};
    fluent::textfields::LineEdit* m_multiTextColorHexEdit{nullptr};
    fluent::layout::Field* m_multiStrokeColorField{nullptr};
    ColorSwatchButton* m_multiStrokeColorSwatch{nullptr};
    fluent::textfields::LineEdit* m_multiStrokeColorHexEdit{nullptr};
    fluent::layout::Field* m_multiClearedField{nullptr};
    fluent::basicinput::ToggleSwitch* m_multiClearedSwitch{nullptr};
    fluent::basicinput::Button* m_multiDeleteButton{nullptr};
};

} // namespace manga::ui::widget
