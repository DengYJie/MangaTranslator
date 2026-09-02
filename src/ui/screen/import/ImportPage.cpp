#include "ui/screen/import/ImportPage.h"

#include "ui/screen/import/PendingSourcesModel.h"

#include <FluentQt/FluentQt.h>
#include <QFileDialog>
#include <QFrame>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyledItemDelegate>

namespace manga::ui::screen::import_ {

namespace {

class ImportProgressBar final : public fluent::status_info::ProgressBar {
public:
    explicit ImportProgressBar(QWidget* parent) : fluent::status_info::ProgressBar(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        fluent::status_info::ProgressBar::resizeEvent(event);
        setBarWidth(event->size().width());
    }
};

void makeScrollSurfaceTransparent(QScrollArea* scrollArea) {
    scrollArea->setAutoFillBackground(false);
    scrollArea->setAttribute(Qt::WA_TranslucentBackground);
    scrollArea->viewport()->setAutoFillBackground(false);
    scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground);

    QPalette palette = scrollArea->palette();
    palette.setColor(QPalette::Window, Qt::transparent);
    palette.setColor(QPalette::Base, Qt::transparent);
    scrollArea->setPalette(palette);
    scrollArea->viewport()->setPalette(palette);
}

} // namespace

class SourcesDelegate : public QStyledItemDelegate {
public:
    explicit SourcesDelegate(ImportPage* page, ImportViewModel* vm, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_page(page), m_vm(vm) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const auto& colors = m_page->themeColorsRef();

        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, colors.subtleSecondary);
        }

        QRect rect = option.rect;
        bool isFolder = index.data(PendingSourcesModel::IsFolderRole).toBool();
        bool supported = index.data(PendingSourcesModel::SupportedRole).toBool();
        QString name = index.data(PendingSourcesModel::DisplayNameRole).toString();
        QString error = index.data(PendingSourcesModel::ErrorSummaryRole).toString();

        int x = rect.left() + 16;
        int y = rect.top() + rect.height() / 2;

        QFont iconFont(Typography::FontFamily::FluentIcons);
        iconFont.setPixelSize(16);
        painter->setFont(iconFont);
        painter->setPen(colors.textPrimary);
        painter->drawText(x, y + 6, isFolder ? QStringLiteral("") : QStringLiteral("")); // Folder or Document

        x += 32;
        painter->setFont(option.font);
        if (!supported) {
            painter->setPen(colors.systemCritical);
            painter->drawText(x, y + 4, name + " (" + error + ")");
        } else {
            painter->setPen(colors.textPrimary);
            painter->drawText(x, y + 4, name);
        }

        // Draw remove icon at the end
        int iconX = rect.right() - 32;
        iconFont.setPixelSize(14);
        painter->setFont(iconFont);
        painter->setPen(colors.textSecondary);
        painter->drawText(iconX, y + 5, QStringLiteral("")); // Cancel

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(200, 48);
    }

    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                int iconX = option.rect.right() - 40;
                if (me->position().x() >= iconX) {
                    QString path = index.data(PendingSourcesModel::PathRole).toString();
                    m_vm->removeSource(path);
                    return true;
                }
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

private:
    ImportPage* m_page;
    ImportViewModel* m_vm;
};

ImportPage::ImportPage(ImportViewModel* viewModel, QWidget* parent) : base::BasePage(parent), m_viewModel(viewModel) {
    setupUi();
    bindViewModel();
}

ImportPage::~ImportPage() = default;

void ImportPage::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* headerLayout = new QVBoxLayout;
    headerLayout->setContentsMargins(24, 24, 24, 16);
    headerLayout->setSpacing(4);
    auto* titleLabel = new fluent::textfields::Label(tr("导入漫画"), this);
    titleLabel->setFluentTypography(Typography::FontRole::TitleLarge);
    auto* subtitleLabel = new fluent::textfields::Label(tr("添加页面并设置本项目的翻译工作流。"), this);
    subtitleLabel->setFluentTypography(Typography::FontRole::Body);
    subtitleLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subtitleLabel);
    rootLayout->addLayout(headerLayout);

    m_infoBar = new fluent::status_info::InfoBar(this);
    m_infoBar->setIsOpen(false);
    m_infoBar->setIsClosable(true);
    m_infoBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_infoBar->setCornerRadius(8);
    m_infoBar->setContentMargins(QMargins(14, 10, 14, 10));
    m_infoBar->setIconTextSpacing(12);
    m_infoBar->setTitleMessageSpacing(8);
    m_infoBar->setSeverityIconSize(20);
    rootLayout->addWidget(m_infoBar);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("importScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    makeScrollSurfaceTransparent(scrollArea);

    auto* scrollWidget = new QWidget(scrollArea);
    scrollWidget->setObjectName(QStringLiteral("scrollWidget"));
    scrollWidget->setAutoFillBackground(false);
    m_mainLayout = new QBoxLayout(QBoxLayout::LeftToRight, scrollWidget);
    m_mainLayout->setContentsMargins(24, 8, 24, 24);
    m_mainLayout->setSpacing(24);
    scrollArea->setWidget(scrollWidget);
    rootLayout->addWidget(scrollArea, 1);

    // Source Pane
    auto* sourceCard = new fluent::layout::Card(scrollWidget);
    sourceCard->setObjectName(QStringLiteral("importSourceCard"));
    sourceCard->setAppearance(fluent::layout::Card::Layer);
    m_sourcePane = sourceCard;
    m_sourcePane->setMinimumHeight(450);
    auto* sourceLayout = new QVBoxLayout(m_sourcePane);
    sourceLayout->setContentsMargins(16, 16, 16, 16);
    sourceLayout->setSpacing(12);

    auto* stageLayout = new QHBoxLayout;
    stageLayout->setSpacing(8);
    const QString stageTexts[3] = {tr("1  选择文件"), tr("2  项目设置"), tr("3  开始导入")};
    for (int i = 0; i < 3; ++i) {
        m_stageLabels[i] = new fluent::textfields::Label(stageTexts[i], this);
        m_stageLabels[i]->setFluentTypography(Typography::FontRole::Body);
        stageLayout->addWidget(m_stageLabels[i]);
        if (i < 2) {
            m_stageConnectors[i] = new QFrame(this);
            m_stageConnectors[i]->setFrameShape(QFrame::HLine);
            m_stageConnectors[i]->setFixedWidth(48);
            stageLayout->addWidget(m_stageConnectors[i], 0, Qt::AlignVCenter);
        }
    }
    stageLayout->addStretch(1);
    sourceLayout->addLayout(stageLayout);

    m_dropZone = new FileDropZone(this);
    m_dropZone->setMinimumHeight(120);

    m_sourceModel = new PendingSourcesModel(m_viewModel, this);
    // Raw QListView: fluent ListView swallows row mouse events, which would break
    // the delegate remove hotspot in editorEvent. Theming lives in SourcesDelegate.
    m_sourceListView = new QListView(this);
    m_sourceListView->setModel(m_sourceModel);
    m_sourceListView->setItemDelegate(new SourcesDelegate(this, m_viewModel, this));
    m_sourceListView->setSelectionMode(QAbstractItemView::NoSelection);
    m_sourceListView->setFrameShape(QFrame::NoFrame);
    m_sourceListView->setAutoFillBackground(false);
    m_sourceListView->viewport()->setAutoFillBackground(false);

    sourceLayout->addWidget(m_dropZone);
    sourceLayout->addWidget(m_sourceListView, 1);

    // Settings Pane
    auto* settingsCard = new fluent::layout::Card(scrollWidget);
    settingsCard->setObjectName(QStringLiteral("importSettingsCard"));
    settingsCard->setAppearance(fluent::layout::Card::Layer);
    m_settingsPane = settingsCard;
    m_settingsPane->setFixedWidth(380);
    auto* settingsLayout = new QVBoxLayout(m_settingsPane);
    settingsLayout->setContentsMargins(16, 16, 16, 16);
    settingsLayout->setSpacing(16);

    auto* projectNameLabel = new fluent::textfields::Label(tr("项目名称"), this);
    m_projectNameEdit = new fluent::textfields::LineEdit(this);
    m_projectNameEdit->setAccessibleName(tr("项目名称"));
    m_nameErrorLabel = new fluent::textfields::Label(this);
    m_nameErrorLabel->setFluentTypography(Typography::FontRole::Caption);
    m_nameErrorLabel->setVisible(false);

    auto* saveLocationLabel = new fluent::textfields::Label(tr("保存位置"), this);
    m_saveLocationEdit = new fluent::textfields::LineEdit(this);
    m_saveLocationEdit->setReadOnly(true);
    m_saveLocationEdit->setAccessibleName(tr("保存位置"));
    m_browseButton = new fluent::basicinput::Button(tr("浏览"), this);
    m_browseButton->setToolTip(tr("选择项目保存文件夹"));
    m_browseButton->setAccessibleName(tr("选择保存位置"));
    m_locationErrorLabel = new fluent::textfields::Label(this);
    m_locationErrorLabel->setFluentTypography(Typography::FontRole::Caption);
    m_locationErrorLabel->setVisible(false);
    auto* saveLocationLayout = new QHBoxLayout;
    saveLocationLayout->addWidget(m_saveLocationEdit, 1);
    saveLocationLayout->addWidget(m_browseButton);

    auto* directionLabel = new fluent::textfields::Label(tr("阅读方向"), this);
    m_rtlRadio = new fluent::basicinput::RadioButton(tr("从右到左（日式漫画）"), this);
    m_rtlRadio->setAccessibleName(tr("从右到左阅读（日式漫画）"));
    m_ltrRadio = new fluent::basicinput::RadioButton(tr("从左到右"), this);
    m_ltrRadio->setAccessibleName(tr("从左到右阅读"));
    auto* directionLayout = new QVBoxLayout;
    directionLayout->addWidget(m_rtlRadio);
    directionLayout->addWidget(m_ltrRadio);

    auto* sourceLangLabel = new fluent::textfields::Label(tr("源语言"), this);
    m_sourceLangCombo = new fluent::basicinput::ComboBox(this);
    m_sourceLangCombo->setAccessibleName(tr("源语言"));
    auto* targetLangLabel = new fluent::textfields::Label(tr("目标语言"), this);
    m_targetLangCombo = new fluent::basicinput::ComboBox(this);
    m_targetLangCombo->setAccessibleName(tr("目标语言"));
    m_languageCautionLabel =
        new fluent::textfields::Label(tr("源语言与目标语言相同：此配置适用于校对、重排和再处理场景。"), this);
    m_languageCautionLabel->setFluentTypography(Typography::FontRole::Caption);
    m_languageCautionLabel->setVisible(false);

    auto setupCombo = [](fluent::basicinput::ComboBox* combo) {
        combo->addItem("日本語", "ja");
        combo->addItem("English", "en");
        combo->addItem("简体中文", "zh-Hans");
        combo->addItem("繁體中文", "zh-Hant");
        combo->addItem("한국어", "ko");
    };
    setupCombo(m_sourceLangCombo);
    setupCombo(m_targetLangCombo);

    m_submitButton = new fluent::basicinput::Button(this);
    m_submitButton->setAccessibleName(tr("创建并导入"));
    m_submitButton->setToolTip(tr("创建项目并导入所选页面"));
    m_cancelImportButton = new fluent::basicinput::Button(this);
    m_cancelImportButton->setText(tr("取消导入"));
    m_cancelImportButton->setAccessibleName(tr("取消导入"));
    m_cancelImportButton->setToolTip(tr("取消正在进行的导入任务"));
    m_cancelImportButton->setVisible(false);
    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(m_submitButton, 1);
    buttonsLayout->addWidget(m_cancelImportButton);

    settingsLayout->addWidget(projectNameLabel);
    settingsLayout->addWidget(m_projectNameEdit);
    settingsLayout->addWidget(m_nameErrorLabel);
    settingsLayout->addWidget(saveLocationLabel);
    settingsLayout->addLayout(saveLocationLayout);
    settingsLayout->addWidget(m_locationErrorLabel);
    settingsLayout->addWidget(directionLabel);
    settingsLayout->addLayout(directionLayout);
    settingsLayout->addWidget(sourceLangLabel);
    settingsLayout->addWidget(m_sourceLangCombo);
    settingsLayout->addWidget(targetLangLabel);
    settingsLayout->addWidget(m_targetLangCombo);
    settingsLayout->addWidget(m_languageCautionLabel);
    settingsLayout->addStretch(1);
    m_progressBar = new ImportProgressBar(m_settingsPane);
    m_progressBar->setAccessibleName(tr("导入进度"));
    m_progressBar->setVisible(false);
    settingsLayout->addWidget(m_progressBar);
    settingsLayout->addLayout(buttonsLayout);

    m_mainLayout->addWidget(m_sourcePane, 1);
    m_mainLayout->addWidget(m_settingsPane);

    applyThemeColors();
    updateStageIndicator();
}

void ImportPage::bindViewModel() {
    connect(m_viewModel, &ImportViewModel::browseFilesRequested, this, [this]() {
        auto paths = QFileDialog::getOpenFileNames(
            this, tr("选择图片文件"), QString(), tr("图片文件 (*.jpg *.jpeg *.png *.webp)"));
        if (!paths.isEmpty()) {
            m_viewModel->addSourcePaths(paths);
        }
    });
    connect(m_viewModel, &ImportViewModel::browseFolderRequested, this, [this]() {
        auto path = QFileDialog::getExistingDirectory(this, tr("选择图片文件夹"));
        if (!path.isEmpty()) {
            m_viewModel->addSourcePaths({path});
        }
    });
    connect(m_viewModel, &ImportViewModel::browseLocationRequested, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择保存位置"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        if (!dir.isEmpty()) {
            m_viewModel->setSaveLocation(dir);
        }
    });
    connect(m_infoBar, &fluent::status_info::InfoBar::closed, m_viewModel, &ImportViewModel::dismissNotice);
    connect(m_viewModel, &ImportViewModel::stateChanged, this, &ImportPage::updateState);

    connect(m_dropZone, &FileDropZone::pathsDropped, m_viewModel, &ImportViewModel::addSourcePaths);
    connect(m_dropZone, &FileDropZone::browseFilesRequested, m_viewModel, &ImportViewModel::requestBrowseFiles);
    connect(m_dropZone, &FileDropZone::browseFolderRequested, m_viewModel, &ImportViewModel::requestBrowseFolder);

    connect(
        m_projectNameEdit, &fluent::textfields::LineEdit::textChanged, m_viewModel, &ImportViewModel::setProjectName);

    connect(m_browseButton, &fluent::basicinput::Button::clicked, m_viewModel, &ImportViewModel::requestBrowseLocation);

    connect(m_rtlRadio, &fluent::basicinput::RadioButton::toggled, this, [this](bool checked) {
        if (checked)
            m_viewModel->setReadingDirection(domain::ReadingDirection::RightToLeft);
    });
    connect(m_ltrRadio, &fluent::basicinput::RadioButton::toggled, this, [this](bool checked) {
        if (checked)
            m_viewModel->setReadingDirection(domain::ReadingDirection::LeftToRight);
    });

    connect(m_sourceLangCombo, &fluent::basicinput::ComboBox::currentIndexChanged, this, [this](int index) {
        m_viewModel->setSourceLanguageCode(m_sourceLangCombo->itemData(index).toString());
    });
    connect(m_targetLangCombo, &fluent::basicinput::ComboBox::currentIndexChanged, this, [this](int index) {
        m_viewModel->setTargetLanguageCode(m_targetLangCombo->itemData(index).toString());
    });

    connect(m_submitButton, &fluent::basicinput::Button::clicked, m_viewModel, &ImportViewModel::submit);
    connect(m_cancelImportButton, &fluent::basicinput::Button::clicked, m_viewModel, &ImportViewModel::cancelImport);

    updateState(m_viewModel->state());
}

void ImportPage::onThemeUpdated() {
    base::BasePage::onThemeUpdated();
    applyThemeColors();
}

void ImportPage::applyThemeColors() {
    const auto& colors = themeColorsRef();
    const QString criticalStyle = QStringLiteral("color: %1;").arg(colors.systemCritical.name());
    const QString cautionStyle = QStringLiteral("color: %1;").arg(colors.systemCaution.name());
    m_nameErrorLabel->setStyleSheet(criticalStyle);
    m_locationErrorLabel->setStyleSheet(criticalStyle);
    m_languageCautionLabel->setStyleSheet(cautionStyle);
    for (auto* connector : m_stageConnectors) {
        QPalette palette = connector->palette();
        palette.setColor(QPalette::WindowText, colors.strokeDefault);
        connector->setPalette(palette);
    }
}

void ImportPage::updateStageIndicator() {
    for (int i = 0; i < 3; ++i) {
        const int stage = i + 1;
        if (stage == m_currentStage) {
            m_stageLabels[i]->setFluentTypography(Typography::FontRole::BodyStrong);
            m_stageLabels[i]->setTextColorRole(fluent::textfields::Label::TextColorRole::Accent);
        } else if (stage < m_currentStage) {
            m_stageLabels[i]->setFluentTypography(Typography::FontRole::Body);
            m_stageLabels[i]->setTextColorRole(fluent::textfields::Label::TextColorRole::Primary);
        } else {
            m_stageLabels[i]->setFluentTypography(Typography::FontRole::Body);
            m_stageLabels[i]->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        }
    }
}

void ImportPage::updateState(const manga::ui::screen::import_::ImportState& state) {
    if (m_projectNameEdit->text() != state.projectName) {
        m_projectNameEdit->setText(state.projectName);
    }
    if (m_saveLocationEdit->text() != state.saveLocation) {
        m_saveLocationEdit->setText(state.saveLocation);
    }

    m_nameErrorLabel->setText(state.nameError);
    m_nameErrorLabel->setVisible(!state.nameError.isEmpty());
    m_locationErrorLabel->setText(state.locationError);
    m_locationErrorLabel->setVisible(!state.locationError.isEmpty());
    m_languageCautionLabel->setVisible(state.languageCaution);

    if (state.readingDirection == domain::ReadingDirection::RightToLeft) {
        m_rtlRadio->setChecked(true);
    } else {
        m_ltrRadio->setChecked(true);
    }

    auto setComboByData = [](fluent::basicinput::ComboBox* combo, const QString& targetData) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == targetData) {
                combo->setCurrentIndex(i);
                break;
            }
        }
    };
    setComboByData(m_sourceLangCombo, state.sourceLanguageCode);
    setComboByData(m_targetLangCombo, state.targetLanguageCode);

    const int stage = state.importInProgress ? 3 : (state.sources.isEmpty() ? 1 : 2);
    if (stage != m_currentStage) {
        m_currentStage = stage;
        updateStageIndicator();
    }

    if (state.importInProgress) {
        m_submitButton->setText(tr("正在导入 %1/%2").arg(state.importedPageCount).arg(state.totalPageCount));
        m_submitButton->setEnabled(false);
        m_cancelImportButton->setVisible(true);
        m_progressBar->setVisible(true);
        m_progressBar->setRange(0, state.totalPageCount);
        m_progressBar->setValue(state.importedPageCount);
        m_dropZone->setEnabled(false);
        m_projectNameEdit->setEnabled(false);
        m_browseButton->setEnabled(false);
        m_rtlRadio->setEnabled(false);
        m_ltrRadio->setEnabled(false);
        m_sourceLangCombo->setEnabled(false);
        m_targetLangCombo->setEnabled(false);
    } else {
        m_progressBar->setVisible(false);
        m_cancelImportButton->setVisible(false);
        m_dropZone->setEnabled(true);
        m_projectNameEdit->setEnabled(true);
        m_browseButton->setEnabled(true);
        m_rtlRadio->setEnabled(true);
        m_ltrRadio->setEnabled(true);
        m_sourceLangCombo->setEnabled(true);
        m_targetLangCombo->setEnabled(true);

        if (state.formErrorCount > 0) {
            m_submitButton->setText(tr("修正问题后导入"));
            m_submitButton->setEnabled(false);
        } else if (state.sources.isEmpty()) {
            m_submitButton->setText(tr("创建并导入"));
            m_submitButton->setEnabled(false);
        } else {
            m_submitButton->setText(tr("创建并导入"));
            m_submitButton->setEnabled(true);
        }
    }

    if (state.notice) {
        m_infoBar->setSeverity(state.notice->severity == ImportNotice::Severity::Error
                                   ? fluent::status_info::InfoBar::Error
                                   : fluent::status_info::InfoBar::Informational);
        m_infoBar->setMessage(state.notice->message);
        m_infoBar->setIsOpen(true);
    } else {
        m_infoBar->setIsOpen(false);
        m_infoBar->setIsClosable(true);
    }
}

void ImportPage::updateResponsiveLayout(int availableWidth) {
    if (availableWidth >= 1008) {
        m_mainLayout->setDirection(QBoxLayout::LeftToRight);
        m_settingsPane->setFixedWidth(380);
    } else {
        m_mainLayout->setDirection(QBoxLayout::TopToBottom);
        m_settingsPane->setMinimumWidth(0);
        m_settingsPane->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

FileDropZone* ImportPage::dropZoneForTest() const {
    return m_dropZone;
}
fluent::basicinput::Button* ImportPage::submitButtonForTest() const {
    return m_submitButton;
}
fluent::basicinput::Button* ImportPage::cancelImportButtonForTest() const {
    return m_cancelImportButton;
}
fluent::status_info::ProgressBar* ImportPage::progressBarForTest() const {
    return m_progressBar;
}

} // namespace manga::ui::screen::import_
