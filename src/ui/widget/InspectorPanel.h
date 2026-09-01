#pragma once

#include "domain/model/Uuid.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/InspectorState.h"

#include <FluentQt/Foundation.h>
#include <QWidget>

class QListWidget;
class QResizeEvent;
class QStackedWidget;

namespace fluent::basicinput {
class Button;
class ComboBox;
class ToggleSwitch;
} // namespace fluent::basicinput
namespace fluent::layout {
class Card;
class Expander;
class Field;
} // namespace fluent::layout
namespace fluent::textfields {
class Label;
class LineEdit;
class NumberBox;
class TextEdit;
} // namespace fluent::textfields
namespace fluent::status_info {
class InfoBar;
}
namespace manga::ui::widget {

class ColorSwatchButton;
class TransparentScrollView;

/// Selection-driven region inspector. Renders the single region property
/// projection and is disabled when unselected. Reports user intent through
/// stable PageId/RegionId/field-enum signals.
class InspectorPanel : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void onThemeUpdated() override;

    /// Renders the inspector projection; enables on single selection, disables on unselected.
    void setInspectorState(const screen::editor::InspectorState& state);
    void setOcrAvailability(bool available, const QString& unavailableReason);
    void setTranslationAvailability(bool available, const QString& unavailableReason);
    void setProcessingState(const screen::editor::EditorProcessingState& processingState);
    void focusTranslationEditor();

    /// Mode of the inspector (None or Single).
    [[nodiscard]] screen::editor::InspectorMode modeForTest() const noexcept {
        return m_mode;
    }
    [[nodiscard]] QWidget* contentHostForTest() const {
        return m_contentHost;
    }
    [[nodiscard]] QWidget* singleHostForTest() const {
        return m_contentHost;
    }

Q_SIGNALS:
    /// A committed, locally validated edit.
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
    void translationSettingsRequested();
    /// A text control lost focus; the ViewModel seals the active text merge batch.
    void textEditingSessionFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void syncContentWidth();
    QWidget* buildContentHost();

    void populateSingle(const screen::editor::SingleRegionInspectorState& state);

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
    void updateTranslationInfoBar();
    bool m_updating{false};
    bool m_inImePreedit{false};
    screen::editor::InspectorMode m_mode{screen::editor::InspectorMode::None};
    /// Identity of the rendered projection, used to build intent signals.
    domain::PageId m_currentPageId;
    QVector<domain::RegionId> m_currentRegionIds;

    fluent::textfields::Label* m_headerTitle{nullptr};
    fluent::textfields::Label* m_headerSubtitle{nullptr};
    TransparentScrollView* m_scrollView{nullptr};
    QWidget* m_contentHost{nullptr};

    // Translation section.
    fluent::layout::Expander* m_translationExpander{nullptr};
    fluent::status_info::InfoBar* m_translationInfoBar{nullptr};
    fluent::basicinput::Button* m_translationSettingsButton{nullptr};
    fluent::textfields::TextEdit* m_sourceEdit{nullptr};
    fluent::textfields::Label* m_sourceMetaLabel{nullptr};
    fluent::textfields::TextEdit* m_translationEdit{nullptr};
    fluent::textfields::Label* m_translationMetaLabel{nullptr};
    fluent::layout::Expander* m_candidatesExpander{nullptr};
    QListWidget* m_candidatesList{nullptr};
    fluent::basicinput::Button* m_adoptCandidateButton{nullptr};

    // Typesetting section.
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

    fluent::basicinput::Button* m_singleTranslateButton{nullptr};

    bool m_ocrAvailable{true};
    QString m_ocrUnavailableReason;
    bool m_translationAvailable{true};
    QString m_translationUnavailableReason;
    screen::editor::EditorProcessingState m_processingState;
};

} // namespace manga::ui::widget
