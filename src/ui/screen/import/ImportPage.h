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
}
namespace fluent::status_info {
class InfoBar;
class ProgressBar;
} // namespace fluent::status_info
namespace fluent::collections {
class ListView;
}

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

    // UI elements
    FileDropZone* m_dropZone;
    fluent::collections::ListView* m_sourceListView;
    PendingSourcesModel* m_sourceModel;

    fluent::textfields::LineEdit* m_projectNameEdit;
    fluent::textfields::LineEdit* m_saveLocationEdit;
    fluent::basicinput::Button* m_browseButton;
    fluent::basicinput::RadioButton* m_rtlRadio;
    fluent::basicinput::RadioButton* m_ltrRadio;
    fluent::basicinput::ComboBox* m_sourceLangCombo;
    fluent::basicinput::ComboBox* m_targetLangCombo;

    fluent::basicinput::Button* m_submitButton;
    fluent::basicinput::Button* m_cancelImportButton;

    void setupUi();
    void bindViewModel();
};

} // namespace manga::ui::screen::import_
