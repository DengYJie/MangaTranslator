#pragma once

#include "ui/base/BasePage.h"
#include "ui/screen/settings/SettingsState.h"

#include <QBoxLayout>
#include <QHash>
#include <QList>
#include <QPointer>

namespace fluent::basicinput {
class Button;
class ComboBox;
class ToggleSwitch;
} // namespace fluent::basicinput

namespace fluent::scrolling {
class ScrollView;
}

namespace fluent::status_info {
class InfoBar;
}

namespace fluent::textfields {
class Label;
class LineEdit;
class NumberBox;
class PasswordBox;
} // namespace fluent::textfields

namespace manga::ui::widget {
class SettingsGroup;
class SettingsRow;
class TransparentScrollView;
} // namespace manga::ui::widget

namespace manga::ui::screen::settings {

class SettingsViewModel;

/// Settings surface page presenting one category within a scrollable and responsive view area.
class SettingsPage : public base::BasePage {
    Q_OBJECT
public:
    explicit SettingsPage(SettingsViewModel* viewModel,
                          SettingsCategory category = SettingsCategory::General,
                          QWidget* parent = nullptr);
    ~SettingsPage() override = default;

    static QWidget* createLazyPage(SettingsViewModel* viewModel, SettingsCategory category, QWidget* parent = nullptr);

    void onThemeUpdated() override;

    [[nodiscard]] SettingsCategory currentCategory() const {
        return m_category;
    }
    [[nodiscard]] widget::SettingsRow* rowForField(const QString& fieldName) const;
    [[nodiscard]] fluent::basicinput::Button* restoreDefaultsButtonForTest() const;

protected:
    void updateResponsiveLayout(int availableWidth) override;

private:
    void buildUi();
    QWidget* buildCategoryView(SettingsCategory category);
    QWidget* buildGeneralView();
    QWidget* buildOcrView();
    QWidget* buildAiTranslationView();
    QWidget* buildAppearanceView();

    void render(const SettingsState& state);
    void updateCategoryHeaders(SettingsCategory category);
    void focusField(const QString& fieldName);
    void showCredentialRemovalDialog();

    SettingsViewModel* m_viewModel; // non-owning
    SettingsCategory m_category{SettingsCategory::General};
    bool m_isUpdatingFromState{false};
    bool m_isCredentialEditMode{false};

    // Chrome & structure
    widget::TransparentScrollView* m_scrollView{nullptr};
    QWidget* m_scrollContent{nullptr};
    QBoxLayout* m_scrollContentLayout{nullptr};
    QWidget* m_columnContainer{nullptr};
    QBoxLayout* m_columnLayout{nullptr};

    fluent::textfields::Label* m_categoryTitleLabel{nullptr};
    fluent::textfields::Label* m_categorySubtitleLabel{nullptr};
    fluent::status_info::InfoBar* m_pageInfoBar{nullptr};

    QWidget* m_categoryView{nullptr};

    // Bottom action bar
    QWidget* m_actionBar{nullptr};
    fluent::basicinput::Button* m_restoreDefaultsButton{nullptr};

    // General category controls
    widget::SettingsRow* m_restoreLastProjectRow{nullptr};
    fluent::basicinput::ToggleSwitch* m_restoreLastProjectSwitch{nullptr};
    widget::SettingsRow* m_recentProjectLimitRow{nullptr};
    fluent::textfields::NumberBox* m_recentProjectLimitBox{nullptr};

    // OCR category controls
    widget::SettingsRow* m_ocrModelDirRow{nullptr};
    fluent::textfields::LineEdit* m_ocrModelDirEdit{nullptr};
    fluent::basicinput::Button* m_ocrBrowseButton{nullptr};
    fluent::status_info::InfoBar* m_ocrStatusInfoBar{nullptr};
    widget::SettingsRow* m_inpaintModelDirRow{nullptr};
    fluent::textfields::LineEdit* m_inpaintModelDirEdit{nullptr};
    fluent::basicinput::Button* m_inpaintBrowseButton{nullptr};
    fluent::status_info::InfoBar* m_inpaintStatusInfoBar{nullptr};
    widget::SettingsRow* m_ocrLanguageRow{nullptr};
    fluent::basicinput::ComboBox* m_ocrLanguageCombo{nullptr};

    // AI Translation category controls
    widget::SettingsRow* m_providerIdRow{nullptr};
    fluent::basicinput::ComboBox* m_providerIdCombo{nullptr};
    widget::SettingsRow* m_endpointRow{nullptr};
    fluent::textfields::LineEdit* m_endpointEdit{nullptr};
    widget::SettingsRow* m_credentialRow{nullptr};
    QWidget* m_credentialConfiguredWidget{nullptr};
    fluent::textfields::Label* m_credentialMaskedLabel{nullptr};
    fluent::basicinput::Button* m_credentialUpdateButton{nullptr};
    fluent::basicinput::Button* m_credentialRemoveButton{nullptr};
    QWidget* m_credentialInputWidget{nullptr};
    fluent::textfields::PasswordBox* m_credentialPasswordBox{nullptr};
    fluent::basicinput::Button* m_credentialSaveButton{nullptr};
    fluent::basicinput::Button* m_credentialCancelButton{nullptr};
    widget::SettingsRow* m_modelNameRow{nullptr};
    fluent::textfields::LineEdit* m_modelNameEdit{nullptr};
    widget::SettingsRow* m_timeoutRow{nullptr};
    fluent::textfields::NumberBox* m_timeoutBox{nullptr};
    widget::SettingsRow* m_retryCountRow{nullptr};
    fluent::textfields::NumberBox* m_retryCountBox{nullptr};

    // Appearance category controls
    widget::SettingsRow* m_themeRow{nullptr};
    fluent::basicinput::ComboBox* m_themeCombo{nullptr};
    widget::SettingsRow* m_densityRow{nullptr};
    fluent::basicinput::ComboBox* m_densityCombo{nullptr};
    widget::SettingsRow* m_reduceMotionRow{nullptr};
    fluent::basicinput::ToggleSwitch* m_reduceMotionSwitch{nullptr};
    widget::SettingsRow* m_canvasBackgroundRow{nullptr};
    fluent::textfields::LineEdit* m_canvasBackgroundEdit{nullptr};

    QHash<QString, widget::SettingsRow*> m_fieldRowMap;
    QList<widget::SettingsGroup*> m_allGroups;
};

} // namespace manga::ui::screen::settings
