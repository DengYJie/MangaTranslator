#pragma once

#include "ui/base/BasePage.h"
#include "ui/screen/import/FileDropZone.h"
#include "ui/screen/import/ImportViewModel.h"

#include <FluentQt/FluentQt.h>

namespace fluent::basicinput {
class Button;
class RadioButton;
class ComboBox;
} // namespace fluent::basicinput
namespace fluent::textfields {
class LineEdit;
class Label;
} // namespace fluent::textfields
namespace fluent::status_info {
class InfoBar;
class ProgressBar;
} // namespace fluent::status_info
class QFrame;
class QListView;

#include <QHBoxLayout>
#include <QStackedLayout>

namespace manga::ui::screen::import_ {

class PendingSourcesModel;
class SourcesDelegate;

class ImportPage : public base::BasePage {
    Q_OBJECT
public:
    explicit ImportPage(ImportViewModel* viewModel, QWidget* parent = nullptr);
    ~ImportPage() override;

    void onThemeUpdated() override;

    FileDropZone* dropZoneForTest() const;
    fluent::basicinput::Button* submitButtonForTest() const;
    fluent::basicinput::Button* cancelImportButtonForTest() const;
    fluent::status_info::ProgressBar* progressBarForTest() const;

protected:
    void updateResponsiveLayout(int availableWidth) override;

private Q_SLOTS:
    void updateState(const ImportState& state);

private:
    ImportViewModel* m_viewModel;

    fluent::status_info::InfoBar* m_infoBar;
    fluent::status_info::ProgressBar* m_progressBar;

    QBoxLayout* m_mainLayout;
    QWidget* m_sourcePane;
    QWidget* m_settingsPane;

    fluent::textfields::Label* m_stageLabels[3];
    QFrame* m_stageConnectors[2];
    int m_currentStage{1};

    // UI elements
    FileDropZone* m_dropZone;
    QListView* m_sourceListView;
    PendingSourcesModel* m_sourceModel;

    fluent::textfields::LineEdit* m_projectNameEdit;
    fluent::textfields::Label* m_nameErrorLabel;
    fluent::textfields::LineEdit* m_saveLocationEdit;
    fluent::textfields::Label* m_locationErrorLabel;
    fluent::textfields::Label* m_languageCautionLabel;
    fluent::basicinput::Button* m_browseButton;
    fluent::basicinput::RadioButton* m_rtlRadio;
    fluent::basicinput::RadioButton* m_ltrRadio;
    fluent::basicinput::ComboBox* m_sourceLangCombo;
    fluent::basicinput::ComboBox* m_targetLangCombo;

    fluent::basicinput::Button* m_submitButton;
    fluent::basicinput::Button* m_cancelImportButton;

    void setupUi();
    void bindViewModel();
    void applyThemeColors();
    void updateStageIndicator();
};

} // namespace manga::ui::screen::import_
