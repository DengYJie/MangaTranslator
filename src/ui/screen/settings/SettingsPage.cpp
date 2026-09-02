#include "ui/screen/settings/SettingsPage.h"

#include "ui/screen/settings/SettingsViewModel.h"
#include "ui/widget/SettingsGroup.h"
#include "ui/widget/SettingsRow.h"
#include "ui/widget/TransparentScrollView.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/Scrolling.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

namespace manga::ui::screen::settings {

namespace {

// ForgeAI SettingsNavigationModule responsive constants
constexpr int kMaxContentWidth = 1064;
constexpr int kMarginWide = 36;
constexpr int kMarginNarrow = 20;
constexpr int kBreakpointWide = 800;
constexpr int kMarginTopWide = 24;
constexpr int kMarginBottomWide = 24;
constexpr int kMarginTopNarrow = 16;
constexpr int kMarginBottomNarrow = 16;

class LazySettingsPage final : public QWidget {
public:
    LazySettingsPage(SettingsViewModel* viewModel, SettingsCategory category, QWidget* parent = nullptr)
        : QWidget(parent), m_viewModel(viewModel), m_category(category) {
        setAutoFillBackground(false);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (m_page != nullptr) {
            return;
        }

        m_page = new SettingsPage(m_viewModel, m_category, this);
        layout()->addWidget(m_page);
    }

private:
    SettingsViewModel* m_viewModel;
    SettingsCategory m_category;
    SettingsPage* m_page{nullptr};
};

} // namespace

SettingsPage::SettingsPage(SettingsViewModel* viewModel, SettingsCategory category, QWidget* parent)
    : BasePage(parent), m_viewModel(viewModel), m_category(category) {
    buildUi();

    if (m_viewModel != nullptr) {
        m_viewModel->observe(this, &SettingsPage::render);
        connect(m_viewModel, &SettingsViewModel::focusFieldRequested, this, &SettingsPage::focusField);
    }
}

QWidget* SettingsPage::createLazyPage(SettingsViewModel* viewModel, SettingsCategory category, QWidget* parent) {
    return new LazySettingsPage(viewModel, category, parent);
}

void SettingsPage::buildUi() {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollView = new widget::TransparentScrollView(this);
    m_scrollView->setWidgetResizable(true);
    m_scrollView->setFrameShape(QFrame::NoFrame);
    m_scrollView->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
    rootLayout->addWidget(m_scrollView);

    m_scrollContent = new QWidget(m_scrollView);
    m_scrollContent->setAutoFillBackground(false);
    m_scrollContentLayout = new QVBoxLayout(m_scrollContent);
    m_scrollContentLayout->setContentsMargins(kMarginWide, kMarginTopWide, kMarginWide, kMarginBottomWide);
    m_scrollContentLayout->setSpacing(0);
    m_scrollContentLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_columnContainer = new QWidget(m_scrollContent);
    m_columnContainer->setAutoFillBackground(false);
    m_columnContainer->setMaximumWidth(kMaxContentWidth);
    m_columnContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_columnLayout = new QVBoxLayout(m_columnContainer);
    m_columnLayout->setContentsMargins(0, 0, 0, 0);
    m_columnLayout->setSpacing(4);
    m_columnLayout->setAlignment(Qt::AlignTop);

    // Category title & subtitle
    m_categoryTitleLabel = new fluent::textfields::Label(m_columnContainer);
    m_categoryTitleLabel->setFluentTypography(Typography::FontRole::Title);
    m_categoryTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_categoryTitleLabel->setObjectName(QStringLiteral("settingsCategoryTitle"));
    m_columnLayout->addWidget(m_categoryTitleLabel);
    m_columnLayout->addSpacing(12);

    m_categorySubtitleLabel = new fluent::textfields::Label(m_columnContainer);
    m_categorySubtitleLabel->setFluentTypography(Typography::FontRole::Body);
    m_categorySubtitleLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_categorySubtitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_categorySubtitleLabel->setObjectName(QStringLiteral("settingsCategorySubtitle"));
    m_columnLayout->addWidget(m_categorySubtitleLabel);
    m_columnLayout->addSpacing(20);
    updateCategoryHeaders(m_category);

    // Global page notification InfoBar
    m_pageInfoBar = new fluent::status_info::InfoBar(m_columnContainer);
    m_pageInfoBar->setIsClosable(false);
    m_pageInfoBar->setIsOpen(false);
    m_columnLayout->addWidget(m_pageInfoBar);

    // Each settings route owns one category page so StackContentHost can animate route changes.
    m_categoryView = buildCategoryView(m_category);
    m_columnLayout->addWidget(m_categoryView);

    m_columnLayout->addStretch(1);

    // Action bar at bottom
    m_actionBar = new QWidget(m_columnContainer);
    m_actionBar->setAutoFillBackground(false);
    auto* actionLayout = new QHBoxLayout(m_actionBar);
    actionLayout->setContentsMargins(0, 8, 0, 0);
    actionLayout->setSpacing(12);

    m_restoreDefaultsButton = new fluent::basicinput::Button(tr("恢复默认"), m_actionBar);
    m_restoreDefaultsButton->setObjectName(QStringLiteral("settingsRestoreDefaultsButton"));
    m_restoreDefaultsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_restoreDefaultsButton->setFixedSize(120, 36);
    m_restoreDefaultsButton->setToolTip(tr("恢复所有设置项为默认值"));
    m_restoreDefaultsButton->setAccessibleName(tr("恢复默认设置"));
    connect(m_restoreDefaultsButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (m_viewModel) {
            m_viewModel->restoreDefaults();
        }
    });
    actionLayout->addStretch(1);
    actionLayout->addWidget(m_restoreDefaultsButton);

    m_columnLayout->addWidget(m_actionBar);

    m_scrollContentLayout->addWidget(m_columnContainer);
    m_scrollView->setContentWidget(m_scrollContent, fluent::WidgetOwnership::Owned);
}

QWidget* SettingsPage::buildCategoryView(SettingsCategory category) {
    switch (category) {
        case SettingsCategory::General:
            return buildGeneralView();
        case SettingsCategory::Ocr:
            return buildOcrView();
        case SettingsCategory::AiTranslation:
            return buildAiTranslationView();
        case SettingsCategory::Appearance:
            return buildAppearanceView();
    }
    return new QWidget(m_columnContainer);
}

QWidget* SettingsPage::buildGeneralView() {
    auto* container = new QWidget();
    container->setAutoFillBackground(false);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* group = new widget::SettingsGroup(tr("常规偏好"), tr("管理项目恢复与最近项目记录"), container);
    m_allGroups.append(group);

    // Restore last project
    m_restoreLastProjectRow =
        new widget::SettingsRow(tr("启动时恢复上次项目"), tr("应用启动时自动打开上次未关闭的项目会话"), group);
    m_restoreLastProjectSwitch = new fluent::basicinput::ToggleSwitch(m_restoreLastProjectRow);
    m_restoreLastProjectSwitch->setObjectName(QStringLiteral("restoreLastProjectSwitch"));
    connect(m_restoreLastProjectSwitch, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setRestoreLastProject(on);
        }
    });
    m_restoreLastProjectRow->setEditorWidget(m_restoreLastProjectSwitch, false);
    group->addRow(m_restoreLastProjectRow);
    m_fieldRowMap.insert(QStringLiteral("restoreLastProject"), m_restoreLastProjectRow);

    // Recent project limit
    m_recentProjectLimitRow =
        new widget::SettingsRow(tr("最近项目数量"), tr("在首页项目列表中显示的最大最近项目条目数（1–50）"), group);
    m_recentProjectLimitBox = new fluent::textfields::NumberBox(m_recentProjectLimitRow);
    m_recentProjectLimitBox->setObjectName(QStringLiteral("recentProjectLimitBox"));
    m_recentProjectLimitBox->setRange(1, 50);
    m_recentProjectLimitBox->setDisplayPrecision(0);
    m_recentProjectLimitBox->setSpinButtonPlacementMode(
        fluent::textfields::NumberBox::SpinButtonPlacementMode::Compact);
    connect(m_recentProjectLimitBox, &fluent::textfields::NumberBox::valueChanged, this, [this](double val) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setRecentProjectLimit(static_cast<int>(val));
        }
    });
    m_recentProjectLimitRow->setEditorWidget(m_recentProjectLimitBox, true);
    group->addRow(m_recentProjectLimitRow);
    m_fieldRowMap.insert(QStringLiteral("recentProjectLimit"), m_recentProjectLimitRow);

    layout->addWidget(group);
    layout->addStretch(1);
    return container;
}

QWidget* SettingsPage::buildOcrView() {
    auto* container = new QWidget();
    container->setAutoFillBackground(false);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* group = new widget::SettingsGroup(
        tr("Baberu OCR 引擎"), tr("配置用于漫画文字识别的 Baberu ONNX 模型与识别语言"), container);
    m_allGroups.append(group);

    // Model directory row
    m_ocrModelDirRow = new widget::SettingsRow(tr("模型目录"), tr("Baberu ONNX 模型文件所在文件夹路径"), group);
    auto* dirEditor = new QWidget(m_ocrModelDirRow);
    auto* dirLayout = new QHBoxLayout(dirEditor);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(8);

    m_ocrModelDirEdit = new fluent::textfields::LineEdit(dirEditor);
    m_ocrModelDirEdit->setObjectName(QStringLiteral("ocrModelDirEdit"));
    m_ocrModelDirEdit->setPlaceholderText(tr("选择或输入模型目录路径"));
    connect(m_ocrModelDirEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setOcrModelDirectory(text.trimmed());
        }
    });
    dirLayout->addWidget(m_ocrModelDirEdit, 1);

    m_ocrBrowseButton = new fluent::basicinput::Button(tr("浏览..."), dirEditor);
    m_ocrBrowseButton->setObjectName(QStringLiteral("ocrBrowseButton"));
    m_ocrBrowseButton->setToolTip(tr("选择 Baberu OCR 模型所在文件夹"));
    m_ocrBrowseButton->setAccessibleName(tr("选择 OCR 模型目录"));
    connect(m_ocrBrowseButton, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择 Baberu OCR 模型目录"));
        if (!dir.isEmpty() && m_viewModel) {
            m_viewModel->setOcrModelDirectory(dir);
        }
    });
    dirLayout->addWidget(m_ocrBrowseButton, 0);

    m_ocrModelDirRow->setEditorWidget(dirEditor, true);
    group->addRow(m_ocrModelDirRow);
    m_fieldRowMap.insert(QStringLiteral("ocrModelDirectory"), m_ocrModelDirRow);

    // Language row
    m_ocrLanguageRow =
        new widget::SettingsRow(tr("默认识别语言"), tr("OCR 识别使用的默认语言（仅对新建项目生效）"), group);
    m_ocrLanguageCombo = new fluent::basicinput::ComboBox(m_ocrLanguageRow);
    m_ocrLanguageCombo->setObjectName(QStringLiteral("ocrLanguageCombo"));
    m_ocrLanguageCombo->addItem(tr("日语 (Japanese)"), QStringLiteral("ja"));
    m_ocrLanguageCombo->addItem(tr("中文 (Chinese)"), QStringLiteral("zh"));
    m_ocrLanguageCombo->addItem(tr("英语 (English)"), QStringLiteral("en"));
    m_ocrLanguageCombo->addItem(tr("韩语 (Korean)"), QStringLiteral("ko"));
    connect(m_ocrLanguageCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_isUpdatingFromState && m_viewModel && index >= 0) {
            m_viewModel->setOcrLanguage(m_ocrLanguageCombo->itemData(index).toString());
        }
    });
    m_ocrLanguageRow->setEditorWidget(m_ocrLanguageCombo, true);
    group->addRow(m_ocrLanguageRow);
    m_fieldRowMap.insert(QStringLiteral("ocrLanguage"), m_ocrLanguageRow);

    layout->addWidget(group);

    // OCR Model Status InfoBar
    m_ocrStatusInfoBar = new fluent::status_info::InfoBar(container);
    m_ocrStatusInfoBar->setIsClosable(false);
    m_ocrStatusInfoBar->setIsOpen(false);
    layout->addWidget(m_ocrStatusInfoBar);

    layout->addSpacing(12);

    auto* detectionGroup = new widget::SettingsGroup(
        tr("版面/文字检测"), tr("配置用于漫画文字框与版面检测的 ONNX 模型 (Koharu Layout)"), container);
    m_allGroups.append(detectionGroup);
    m_detectionModelDirRow =
        new widget::SettingsRow(tr("检测模型目录"), tr("包含 manifest.json 的版面检测模型文件夹路径"), detectionGroup);
    auto* detectionEditor = new QWidget(m_detectionModelDirRow);
    auto* detectionLayout = new QHBoxLayout(detectionEditor);
    detectionLayout->setContentsMargins(0, 0, 0, 0);
    detectionLayout->setSpacing(8);
    m_detectionModelDirEdit = new fluent::textfields::LineEdit(detectionEditor);
    m_detectionModelDirEdit->setObjectName(QStringLiteral("detectionModelDirEdit"));
    m_detectionModelDirEdit->setPlaceholderText(tr("选择或输入检测模型目录路径"));
    connect(m_detectionModelDirEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel)
            m_viewModel->setDetectionModelDirectory(text.trimmed());
    });
    detectionLayout->addWidget(m_detectionModelDirEdit, 1);
    m_detectionBrowseButton = new fluent::basicinput::Button(tr("浏览..."), detectionEditor);
    m_detectionBrowseButton->setObjectName(QStringLiteral("detectionBrowseButton"));
    m_detectionBrowseButton->setAccessibleName(tr("选择版面检测模型目录"));
    connect(m_detectionBrowseButton, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择版面检测模型目录"));
        if (!dir.isEmpty() && m_viewModel)
            m_viewModel->setDetectionModelDirectory(dir);
    });
    detectionLayout->addWidget(m_detectionBrowseButton, 0);
    m_detectionModelDirRow->setEditorWidget(detectionEditor, true);
    detectionGroup->addRow(m_detectionModelDirRow);
    m_fieldRowMap.insert(QStringLiteral("detectionModelDirectory"), m_detectionModelDirRow);
    layout->addWidget(detectionGroup);

    m_detectionStatusInfoBar = new fluent::status_info::InfoBar(container);
    m_detectionStatusInfoBar->setIsClosable(false);
    m_detectionStatusInfoBar->setIsOpen(false);
    layout->addWidget(m_detectionStatusInfoBar);

    layout->addSpacing(12);

    auto* inpaintGroup = new widget::SettingsGroup(
        tr("Remove 图像修复"), tr("配置用于 Remove 工具的 dynamic spatial ONNX 模型"), container);
    m_allGroups.append(inpaintGroup);
    m_inpaintModelDirRow =
        new widget::SettingsRow(tr("模型目录"), tr("Inpaint ONNX 模型文件所在文件夹路径"), inpaintGroup);
    auto* inpaintEditor = new QWidget(m_inpaintModelDirRow);
    auto* inpaintLayout = new QHBoxLayout(inpaintEditor);
    inpaintLayout->setContentsMargins(0, 0, 0, 0);
    inpaintLayout->setSpacing(8);
    m_inpaintModelDirEdit = new fluent::textfields::LineEdit(inpaintEditor);
    m_inpaintModelDirEdit->setObjectName(QStringLiteral("inpaintModelDirEdit"));
    m_inpaintModelDirEdit->setPlaceholderText(tr("选择或输入模型目录路径"));
    connect(m_inpaintModelDirEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel)
            m_viewModel->setInpaintModelDirectory(text.trimmed());
    });
    inpaintLayout->addWidget(m_inpaintModelDirEdit, 1);
    m_inpaintBrowseButton = new fluent::basicinput::Button(tr("浏览..."), inpaintEditor);
    m_inpaintBrowseButton->setObjectName(QStringLiteral("inpaintBrowseButton"));
    m_inpaintBrowseButton->setAccessibleName(tr("选择 Inpaint 模型目录"));
    connect(m_inpaintBrowseButton, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择 Inpaint 模型目录"));
        if (!dir.isEmpty() && m_viewModel)
            m_viewModel->setInpaintModelDirectory(dir);
    });
    inpaintLayout->addWidget(m_inpaintBrowseButton, 0);
    m_inpaintModelDirRow->setEditorWidget(inpaintEditor, true);
    inpaintGroup->addRow(m_inpaintModelDirRow);
    m_fieldRowMap.insert(QStringLiteral("inpaintModelDirectory"), m_inpaintModelDirRow);
    layout->addWidget(inpaintGroup);

    m_inpaintStatusInfoBar = new fluent::status_info::InfoBar(container);
    m_inpaintStatusInfoBar->setIsClosable(false);
    m_inpaintStatusInfoBar->setIsOpen(false);
    layout->addWidget(m_inpaintStatusInfoBar);

    layout->addStretch(1);
    return container;
}

QWidget* SettingsPage::buildAiTranslationView() {
    auto* container = new QWidget();
    container->setAutoFillBackground(false);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* group =
        new widget::SettingsGroup(tr("服务商配置"), tr("配置用于漫画台词翻译的 AI 模型、端点与访问密钥"), container);
    m_allGroups.append(group);

    // Provider row
    m_providerIdRow = new widget::SettingsRow(tr("服务商"), tr("选择翻译所使用的 AI 服务商"), group);
    m_providerIdCombo = new fluent::basicinput::ComboBox(m_providerIdRow);
    m_providerIdCombo->setObjectName(QStringLiteral("providerIdCombo"));
    m_providerIdCombo->addItem(tr("OpenAI 兼容 (OpenAI-Compatible)"), QStringLiteral("openai-compatible"));
    connect(m_providerIdCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_isUpdatingFromState && m_viewModel && index >= 0) {
            m_viewModel->setProviderId(m_providerIdCombo->itemData(index).toString());
        }
    });
    m_providerIdRow->setEditorWidget(m_providerIdCombo, true);
    group->addRow(m_providerIdRow);
    m_fieldRowMap.insert(QStringLiteral("providerId"), m_providerIdRow);

    // Endpoint row
    m_endpointRow = new widget::SettingsRow(tr("API 端点"), tr("API 基础 URL，留空使用服务商默认端点"), group);
    m_endpointEdit = new fluent::textfields::LineEdit(m_endpointRow);
    m_endpointEdit->setObjectName(QStringLiteral("endpointEdit"));
    m_endpointEdit->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    connect(m_endpointEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setEndpoint(text.trimmed());
        }
    });
    m_endpointRow->setEditorWidget(m_endpointEdit, true);
    group->addRow(m_endpointRow);
    m_fieldRowMap.insert(QStringLiteral("endpoint"), m_endpointRow);

    // API Key Row (Secure Credential management)
    m_credentialRow = new widget::SettingsRow(
        tr("API 密钥"), tr("访问服务商所需的 API 密钥。密钥安全保存在本地凭据库中，绝不记录明文日志。"), group);

    auto* credentialContainer = new QWidget(m_credentialRow);
    auto* credentialStackLayout = new QVBoxLayout(credentialContainer);
    credentialStackLayout->setContentsMargins(0, 0, 0, 0);
    credentialStackLayout->setSpacing(0);

    // Configured sub-widget
    m_credentialConfiguredWidget = new QWidget(credentialContainer);
    auto* confLayout = new QHBoxLayout(m_credentialConfiguredWidget);
    confLayout->setContentsMargins(0, 0, 0, 0);
    confLayout->setSpacing(8);

    m_credentialMaskedLabel = new fluent::textfields::Label(m_credentialConfiguredWidget);
    m_credentialMaskedLabel->setText(tr("●●●●●●●● (已配置)"));
    m_credentialMaskedLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    confLayout->addWidget(m_credentialMaskedLabel);

    m_credentialUpdateButton = new fluent::basicinput::Button(tr("更新密钥"), m_credentialConfiguredWidget);
    m_credentialUpdateButton->setObjectName(QStringLiteral("credentialUpdateButton"));
    m_credentialUpdateButton->setToolTip(tr("输入并配置新的 API 密钥"));
    m_credentialUpdateButton->setAccessibleName(tr("更新 API 密钥"));
    connect(m_credentialUpdateButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_isCredentialEditMode = true;
        m_credentialConfiguredWidget->setVisible(false);
        m_credentialInputWidget->setVisible(true);
        m_credentialPasswordBox->clear();
        m_credentialPasswordBox->setFocus();
    });
    confLayout->addWidget(m_credentialUpdateButton);

    m_credentialRemoveButton = new fluent::basicinput::Button(tr("移除密钥"), m_credentialConfiguredWidget);
    m_credentialRemoveButton->setObjectName(QStringLiteral("credentialRemoveButton"));
    m_credentialRemoveButton->setToolTip(tr("从本地安全存储中移除该密钥"));
    m_credentialRemoveButton->setAccessibleName(tr("移除 API 密钥"));
    connect(m_credentialRemoveButton,
            &fluent::basicinput::Button::clicked,
            this,
            &SettingsPage::showCredentialRemovalDialog);
    confLayout->addWidget(m_credentialRemoveButton);

    credentialStackLayout->addWidget(m_credentialConfiguredWidget);

    // Input sub-widget
    m_credentialInputWidget = new QWidget(credentialContainer);
    auto* inputLayout = new QHBoxLayout(m_credentialInputWidget);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(8);

    m_credentialPasswordBox = new fluent::textfields::PasswordBox(m_credentialInputWidget);
    m_credentialPasswordBox->setObjectName(QStringLiteral("credentialPasswordBox"));
    m_credentialPasswordBox->setPlaceholderText(tr("输入新的 API 密钥"));
    m_credentialPasswordBox->setAccessibleName(tr("API 密钥"));
    inputLayout->addWidget(m_credentialPasswordBox, 1);

    m_credentialSaveButton = new fluent::basicinput::Button(tr("保存密钥"), m_credentialInputWidget);
    m_credentialSaveButton->setObjectName(QStringLiteral("credentialSaveButton"));
    m_credentialSaveButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_credentialSaveButton->setToolTip(tr("安全保存 API 密钥"));
    m_credentialSaveButton->setAccessibleName(tr("保存 API 密钥"));
    connect(m_credentialSaveButton, &fluent::basicinput::Button::clicked, this, [this]() {
        const QString key = m_credentialPasswordBox->password();
        if (!key.isEmpty() && m_viewModel) {
            m_viewModel->setCredential(application::SecretBytes(key.toUtf8()));
        }
        m_credentialPasswordBox->clear();
        m_isCredentialEditMode = false;
    });
    inputLayout->addWidget(m_credentialSaveButton);

    m_credentialCancelButton = new fluent::basicinput::Button(tr("取消"), m_credentialInputWidget);
    m_credentialCancelButton->setObjectName(QStringLiteral("credentialCancelButton"));
    m_credentialCancelButton->setToolTip(tr("取消密钥修改"));
    m_credentialCancelButton->setAccessibleName(tr("取消密钥修改"));
    connect(m_credentialCancelButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_credentialPasswordBox->clear();
        m_isCredentialEditMode = false;
        render(m_viewModel->state());
    });
    inputLayout->addWidget(m_credentialCancelButton);

    credentialStackLayout->addWidget(m_credentialInputWidget);

    m_credentialRow->setEditorWidget(credentialContainer, true);
    group->addRow(m_credentialRow);
    m_fieldRowMap.insert(QStringLiteral("credential"), m_credentialRow);

    // Default model ID string row
    m_modelNameRow =
        new widget::SettingsRow(tr("默认模型"), tr("翻译所使用的模型标识符（例如 gpt-4o, deepseek-chat 等）"), group);
    m_modelNameEdit = new fluent::textfields::LineEdit(m_modelNameRow);
    m_modelNameEdit->setObjectName(QStringLiteral("modelNameEdit"));
    m_modelNameEdit->setPlaceholderText(QStringLiteral("gpt-4o"));
    connect(m_modelNameEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setModelName(text.trimmed());
        }
    });
    m_modelNameRow->setEditorWidget(m_modelNameEdit, true);
    group->addRow(m_modelNameRow);
    m_fieldRowMap.insert(QStringLiteral("modelName"), m_modelNameRow);

    // Timeout row
    m_timeoutRow = new widget::SettingsRow(tr("请求超时"), tr("单个翻译请求的超时时间（1–300 秒）"), group);
    m_timeoutBox = new fluent::textfields::NumberBox(m_timeoutRow);
    m_timeoutBox->setObjectName(QStringLiteral("timeoutBox"));
    m_timeoutBox->setRange(1, 300);
    m_timeoutBox->setDisplayPrecision(0);
    m_timeoutBox->setSmallChange(5);
    m_timeoutBox->setSpinButtonPlacementMode(fluent::textfields::NumberBox::SpinButtonPlacementMode::Compact);
    connect(m_timeoutBox, &fluent::textfields::NumberBox::valueChanged, this, [this](double val) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setTimeoutSeconds(static_cast<int>(val));
        }
    });
    m_timeoutRow->setEditorWidget(m_timeoutBox, true);
    group->addRow(m_timeoutRow);
    m_fieldRowMap.insert(QStringLiteral("timeoutSeconds"), m_timeoutRow);

    // Retry count row
    m_retryCountRow = new widget::SettingsRow(tr("重试次数"), tr("请求失败时的自动重试次数（0–10 次）"), group);
    m_retryCountBox = new fluent::textfields::NumberBox(m_retryCountRow);
    m_retryCountBox->setObjectName(QStringLiteral("retryCountBox"));
    m_retryCountBox->setRange(0, 10);
    m_retryCountBox->setDisplayPrecision(0);
    m_retryCountBox->setSmallChange(1);
    m_retryCountBox->setSpinButtonPlacementMode(fluent::textfields::NumberBox::SpinButtonPlacementMode::Compact);
    connect(m_retryCountBox, &fluent::textfields::NumberBox::valueChanged, this, [this](double val) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setRetryCount(static_cast<int>(val));
        }
    });
    m_retryCountRow->setEditorWidget(m_retryCountBox, true);
    group->addRow(m_retryCountRow);
    m_fieldRowMap.insert(QStringLiteral("retryCount"), m_retryCountRow);

    layout->addWidget(group);
    layout->addStretch(1);
    return container;
}

QWidget* SettingsPage::buildAppearanceView() {
    auto* container = new QWidget();
    container->setAutoFillBackground(false);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* group = new widget::SettingsGroup(
        tr("主题与外观"), tr("自定义应用界面主题、显示密度与视觉偏好（修改将即时预览）"), container);
    m_allGroups.append(group);

    // Theme row
    m_themeRow = new widget::SettingsRow(tr("应用主题"), tr("选择界面色彩模式（更改将即时预览）"), group);
    m_themeCombo = new fluent::basicinput::ComboBox(m_themeRow);
    m_themeCombo->setObjectName(QStringLiteral("themeCombo"));
    m_themeCombo->addItem(tr("跟随系统 (System)"), static_cast<int>(application::AppTheme::System));
    m_themeCombo->addItem(tr("浅色 (Light)"), static_cast<int>(application::AppTheme::Light));
    m_themeCombo->addItem(tr("深色 (Dark)"), static_cast<int>(application::AppTheme::Dark));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_isUpdatingFromState && m_viewModel && index >= 0) {
            const auto theme = static_cast<application::AppTheme>(m_themeCombo->itemData(index).toInt());
            m_viewModel->setTheme(theme);
        }
    });
    m_themeRow->setEditorWidget(m_themeCombo, true);
    group->addRow(m_themeRow);
    m_fieldRowMap.insert(QStringLiteral("theme"), m_themeRow);

    // Density row
    m_densityRow = new widget::SettingsRow(tr("界面密度"), tr("选择控件的紧凑或舒适间距布局"), group);
    m_densityCombo = new fluent::basicinput::ComboBox(m_densityRow);
    m_densityCombo->setObjectName(QStringLiteral("densityCombo"));
    m_densityCombo->addItem(tr("紧凑 (Compact)"), static_cast<int>(application::InterfaceDensity::Compact));
    m_densityCombo->addItem(tr("舒适 (Comfortable)"), static_cast<int>(application::InterfaceDensity::Comfortable));
    connect(m_densityCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_isUpdatingFromState && m_viewModel && index >= 0) {
            const auto density = static_cast<application::InterfaceDensity>(m_densityCombo->itemData(index).toInt());
            m_viewModel->setDensity(density);
        }
    });
    m_densityRow->setEditorWidget(m_densityCombo, true);
    group->addRow(m_densityRow);
    m_fieldRowMap.insert(QStringLiteral("density"), m_densityRow);

    // Reduce motion row
    m_reduceMotionRow = new widget::SettingsRow(tr("减少动效"), tr("减少界面切换与操作中的过渡动画"), group);
    m_reduceMotionSwitch = new fluent::basicinput::ToggleSwitch(m_reduceMotionRow);
    m_reduceMotionSwitch->setObjectName(QStringLiteral("reduceMotionSwitch"));
    connect(m_reduceMotionSwitch, &fluent::basicinput::ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setReduceMotion(on);
        }
    });
    m_reduceMotionRow->setEditorWidget(m_reduceMotionSwitch, false);
    group->addRow(m_reduceMotionRow);
    m_fieldRowMap.insert(QStringLiteral("reduceMotion"), m_reduceMotionRow);

    // Canvas background color row
    m_canvasBackgroundRow = new widget::SettingsRow(
        tr("画布背景"), tr("编辑器漫画画布的工作区背景色（十六进制颜色代码，例如 #202124）"), group);
    m_canvasBackgroundEdit = new fluent::textfields::LineEdit(m_canvasBackgroundRow);
    m_canvasBackgroundEdit->setObjectName(QStringLiteral("canvasBackgroundEdit"));
    m_canvasBackgroundEdit->setPlaceholderText(QStringLiteral("#202124"));
    connect(m_canvasBackgroundEdit, &fluent::textfields::LineEdit::textChanged, this, [this](const QString& text) {
        if (!m_isUpdatingFromState && m_viewModel) {
            m_viewModel->setCanvasBackground(text.trimmed());
        }
    });
    m_canvasBackgroundRow->setEditorWidget(m_canvasBackgroundEdit, true);
    group->addRow(m_canvasBackgroundRow);
    m_fieldRowMap.insert(QStringLiteral("canvasBackground"), m_canvasBackgroundRow);

    layout->addWidget(group);
    layout->addStretch(1);
    return container;
}

void SettingsPage::render(const SettingsState& state) {
    m_isUpdatingFromState = true;

    // Update controls owned by this category page.
    if (m_restoreLastProjectSwitch) {
        m_restoreLastProjectSwitch->setIsOn(state.persisted.restoreLastProject);
    }
    if (m_recentProjectLimitBox) {
        m_recentProjectLimitBox->setValue(state.persisted.recentProjectLimit);
    }

    // Update OCR category inputs.
    if (m_ocrModelDirEdit && m_ocrModelDirEdit->text() != state.persisted.ocrModelDirectory) {
        m_ocrModelDirEdit->setText(state.persisted.ocrModelDirectory);
    }
    if (m_ocrLanguageCombo) {
        const int langIdx = m_ocrLanguageCombo->findData(state.persisted.ocrLanguage);
        if (langIdx >= 0 && m_ocrLanguageCombo->currentIndex() != langIdx) {
            m_ocrLanguageCombo->setCurrentIndex(langIdx);
        }
    }

    if (m_detectionModelDirEdit && m_detectionModelDirEdit->text() != state.persisted.detectionModelDirectory)
        m_detectionModelDirEdit->setText(state.persisted.detectionModelDirectory);
    if (m_detectionStatusInfoBar) {
        if (state.persisted.detectionModelDirectory.trimmed().isEmpty()) {
            m_detectionStatusInfoBar->setIsOpen(false);
        } else if (state.detectionModelStatus.isValid) {
            m_detectionStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Success);
            m_detectionStatusInfoBar->setTitle(tr("版面检测模型有效"));
            m_detectionStatusInfoBar->setMessage(
                tr("模型: %1 (版本: %2)").arg(state.detectionModelStatus.id, state.detectionModelStatus.version));
            m_detectionStatusInfoBar->setIsOpen(true);
        } else {
            m_detectionStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Error);
            m_detectionStatusInfoBar->setTitle(tr("版面检测模型校验失败"));
            m_detectionStatusInfoBar->setMessage(
                state.fieldErrors.value(QStringLiteral("detectionModelDirectory"), tr("未找到有效的版面检测模型文件")));
            m_detectionStatusInfoBar->setIsOpen(true);
        }
    }

    if (m_inpaintModelDirEdit && m_inpaintModelDirEdit->text() != state.persisted.inpaintModelDirectory)
        m_inpaintModelDirEdit->setText(state.persisted.inpaintModelDirectory);
    if (m_inpaintStatusInfoBar) {
        if (state.persisted.inpaintModelDirectory.trimmed().isEmpty()) {
            m_inpaintStatusInfoBar->setIsOpen(false);
        } else if (state.inpaintModelStatus.isValid) {
            m_inpaintStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Success);
            m_inpaintStatusInfoBar->setTitle(tr("Inpaint 模型有效"));
            m_inpaintStatusInfoBar->setMessage(
                tr("模型: %1 (版本: %2)").arg(state.inpaintModelStatus.id, state.inpaintModelStatus.version));
            m_inpaintStatusInfoBar->setIsOpen(true);
        } else {
            m_inpaintStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Error);
            m_inpaintStatusInfoBar->setTitle(tr("Inpaint 模型校验失败"));
            m_inpaintStatusInfoBar->setMessage(
                state.fieldErrors.value(QStringLiteral("inpaintModelDirectory"), tr("未找到有效的 Inpaint 模型文件")));
            m_inpaintStatusInfoBar->setIsOpen(true);
        }
    }

    // Update OCR status InfoBar.
    if (m_ocrStatusInfoBar) {
        if (state.persisted.ocrModelDirectory.trimmed().isEmpty()) {
            m_ocrStatusInfoBar->setIsOpen(false);
        } else if (state.modelStatus.isValid) {
            m_ocrStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Success);
            m_ocrStatusInfoBar->setTitle(tr("OCR 模型有效"));
            m_ocrStatusInfoBar->setMessage(tr("模型: %1 (版本: %2, 算子集: %3)")
                                               .arg(state.modelStatus.id)
                                               .arg(state.modelStatus.version)
                                               .arg(state.modelStatus.opset));
            m_ocrStatusInfoBar->setIsOpen(true);
        } else {
            m_ocrStatusInfoBar->setSeverity(fluent::status_info::InfoBar::Error);
            m_ocrStatusInfoBar->setTitle(tr("OCR 模型校验失败"));
            m_ocrStatusInfoBar->setMessage(
                state.fieldErrors.value(QStringLiteral("ocrModelDirectory"), tr("未找到有效的 Baberu OCR 模型文件")));
            m_ocrStatusInfoBar->setIsOpen(true);
        }
    }

    // Update AI Translation category inputs.
    if (m_providerIdCombo) {
        const int provIdx = m_providerIdCombo->findData(state.persisted.providerId);
        if (provIdx >= 0 && m_providerIdCombo->currentIndex() != provIdx) {
            m_providerIdCombo->setCurrentIndex(provIdx);
        }
    }
    if (m_endpointEdit && m_endpointEdit->text() != state.persisted.endpoint) {
        m_endpointEdit->setText(state.persisted.endpoint);
    }
    if (m_modelNameEdit && m_modelNameEdit->text() != state.persisted.modelName) {
        m_modelNameEdit->setText(state.persisted.modelName);
    }
    if (m_timeoutBox) {
        m_timeoutBox->setValue(state.persisted.timeoutSeconds);
    }
    if (m_retryCountBox) {
        m_retryCountBox->setValue(state.persisted.retryCount);
    }

    // Update credential status and widget visibility.
    if (m_credentialConfiguredWidget && m_credentialInputWidget && m_credentialCancelButton) {
        if (state.credentialStatus == application::CredentialState::Configured && !m_isCredentialEditMode) {
            m_credentialConfiguredWidget->setVisible(true);
            m_credentialInputWidget->setVisible(false);
            m_credentialCancelButton->setVisible(true);
        } else {
            m_credentialConfiguredWidget->setVisible(false);
            m_credentialInputWidget->setVisible(true);
            m_credentialCancelButton->setVisible(state.credentialStatus == application::CredentialState::Configured);
        }
    }

    // Update Appearance category inputs.
    if (m_themeCombo) {
        const int themeIdx = m_themeCombo->findData(static_cast<int>(state.persisted.theme));
        if (themeIdx >= 0 && m_themeCombo->currentIndex() != themeIdx) {
            m_themeCombo->setCurrentIndex(themeIdx);
        }
    }
    if (m_densityCombo) {
        const int densityIdx = m_densityCombo->findData(static_cast<int>(state.persisted.density));
        if (densityIdx >= 0 && m_densityCombo->currentIndex() != densityIdx) {
            m_densityCombo->setCurrentIndex(densityIdx);
        }
    }
    if (m_reduceMotionSwitch) {
        m_reduceMotionSwitch->setIsOn(state.persisted.reduceMotion);
    }
    if (m_canvasBackgroundEdit && m_canvasBackgroundEdit->text() != state.persisted.canvasBackground) {
        m_canvasBackgroundEdit->setText(state.persisted.canvasBackground);
    }

    // Update field errors
    for (auto it = m_fieldRowMap.begin(); it != m_fieldRowMap.end(); ++it) {
        const QString& field = it.key();
        auto* row = it.value();
        if (!row) {
            continue;
        }

        if (state.fieldErrors.contains(field)) {
            row->setError(state.fieldErrors.value(field));
        } else {
            row->clearError();
        }
    }

    // Show persistence failures without introducing a second save state.
    if (state.fieldErrors.contains(QStringLiteral("persist"))) {
        m_pageInfoBar->setSeverity(fluent::status_info::InfoBar::Error);
        m_pageInfoBar->setTitle(tr("保存失败"));
        m_pageInfoBar->setMessage(state.fieldErrors.value(QStringLiteral("persist")));
        m_pageInfoBar->setIsOpen(true);
    } else {
        m_pageInfoBar->setIsOpen(false);
    }

    m_isUpdatingFromState = false;
}

void SettingsPage::updateCategoryHeaders(SettingsCategory category) {
    switch (category) {
        case SettingsCategory::General:
            m_categoryTitleLabel->setText(tr("常规"));
            m_categorySubtitleLabel->setText(tr("配置应用启动、恢复与项目记录等常规偏好。"));
            break;
        case SettingsCategory::Ocr:
            m_categoryTitleLabel->setText(tr("OCR 与本地模型"));
            m_categorySubtitleLabel->setText(tr(
                "配置漫画台词识别 (Baberu OCR)、版面与文字检测 (Koharu Layout) 以及图像修复 (Inpaint) 的 ONNX 模型。"));
            break;
        case SettingsCategory::AiTranslation:
            m_categoryTitleLabel->setText(tr("AI 翻译"));
            m_categorySubtitleLabel->setText(tr("配置大语言模型服务商、安全凭据、请求参数与模型标识。"));
            break;
        case SettingsCategory::Appearance:
            m_categoryTitleLabel->setText(tr("外观与主题"));
            m_categorySubtitleLabel->setText(tr("配置应用色彩模式、界面密度、动效偏好与画布背景。"));
            break;
    }
}

void SettingsPage::focusField(const QString& fieldName) {
    auto* row = m_fieldRowMap.value(fieldName, nullptr);
    if (row && row->editorWidget()) {
        row->editorWidget()->setFocus();
        if (m_scrollView) {
            m_scrollView->ensureWidgetVisible(row);
        }
    }
}

void SettingsPage::showCredentialRemovalDialog() {
    auto* dialog = new fluent::dialogs_flyouts::ContentDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setTitle(tr("确认移除 API 密钥？"));
    dialog->setContent(new QLabel(tr("移除密钥后，该服务商的 AI 翻译功能将无法使用，直到重新配置新的密钥。")));
    dialog->setPrimaryButtonText(tr("移除"));
    dialog->setCloseButtonText(tr("取消"));
    dialog->setDefaultButton(fluent::dialogs_flyouts::ContentDialog::Close);

    connect(dialog, &fluent::dialogs_flyouts::ContentDialog::primaryButtonClicked, this, [this]() {
        if (m_viewModel) {
            m_viewModel->removeCredential();
        }
    });

    dialog->show();
}

widget::SettingsRow* SettingsPage::rowForField(const QString& fieldName) const {
    return m_fieldRowMap.value(fieldName, nullptr);
}

fluent::basicinput::Button* SettingsPage::restoreDefaultsButtonForTest() const {
    return m_restoreDefaultsButton;
}

void SettingsPage::updateResponsiveLayout(int availableWidth) {
    BasePage::updateResponsiveLayout(availableWidth);

    const int viewportWidth =
        m_scrollView && m_scrollView->viewport() ? m_scrollView->viewport()->width() : availableWidth;
    if (m_scrollContent) {
        m_scrollContent->setMinimumWidth(qMax(0, viewportWidth));
    }

    // Match ForgeAI's full-width scroll content with a centered, max-width column.
    const bool narrow = viewportWidth < kBreakpointWide;
    const int marginH = narrow ? kMarginNarrow : kMarginWide;
    const int marginV = narrow ? kMarginTopNarrow : kMarginTopWide;
    const int marginB = narrow ? kMarginBottomNarrow : kMarginBottomWide;

    m_scrollContentLayout->setContentsMargins(marginH, marginV, marginH, marginB);

    const int contentWidth = qMin(qMax(0, viewportWidth - marginH * 2), kMaxContentWidth);
    for (auto* group : m_allGroups) {
        if (group) {
            group->updateResponsiveLayout(contentWidth);
        }
    }
}

void SettingsPage::onThemeUpdated() {
    BasePage::onThemeUpdated();
    for (auto* group : m_allGroups) {
        if (group) {
            group->onThemeUpdated();
        }
    }
}

} // namespace manga::ui::screen::settings
