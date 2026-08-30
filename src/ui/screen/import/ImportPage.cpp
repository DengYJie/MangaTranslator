#include "ui/screen/import/ImportPage.h"

#include "ui/screen/import/PendingSourcesModel.h"

#include <FluentQt/FluentQt.h>
#include <QFileDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QStyledItemDelegate>

namespace manga::ui::screen::import_ {

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
        painter->drawText(
            x, y + 6, isFolder ? QStringLiteral("\uE8B7") : QStringLiteral("\uE8B5")); // Folder or Document

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
        painter->drawText(iconX, y + 5, QStringLiteral("\uE711")); // Cancel

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

    m_infoBar = new fluent::status_info::InfoBar(this);
    m_infoBar->setIsOpen(false);
    rootLayout->addWidget(m_infoBar);

    m_progressBar = new fluent::status_info::ProgressBar(this);
    m_progressBar->setVisible(false);
    rootLayout->addWidget(m_progressBar);

    m_mainLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_mainLayout->setContentsMargins(24, 24, 24, 24);
    m_mainLayout->setSpacing(24);
    rootLayout->addLayout(m_mainLayout);

    // Source Pane
    m_sourcePane = new QWidget(this);
    auto* sourceLayout = new QVBoxLayout(m_sourcePane);
    sourceLayout->setContentsMargins(0, 0, 0, 0);

    m_dropZone = new FileDropZone(this);
    m_dropZone->setMinimumHeight(120);

    m_sourceModel = new PendingSourcesModel(m_viewModel, this);
    m_sourceListView = new fluent::collections::ListView(this);
    m_sourceListView->setModel(m_sourceModel);
    m_sourceListView->setItemDelegate(new SourcesDelegate(this, m_viewModel, this));
    m_sourceListView->setSelectionMode(fluent::collections::ListView::SelectionMode::None);

    sourceLayout->addWidget(m_dropZone);
    sourceLayout->addWidget(m_sourceListView, 1);

    // Settings Pane
    m_settingsPane = new QWidget(this);
    m_settingsPane->setFixedWidth(380);
    auto* settingsLayout = new QVBoxLayout(m_settingsPane);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(16);

    m_projectNameEdit = new fluent::textfields::LineEdit(this);
    m_projectNameEdit->setPlaceholderText(
        tr("项目名称")); // Actually need explicit labels but LineEdit in FluentQt usually has a placeholder or label?
                         // The brief says "设置面板字段（显式标签，不靠 Placeholder）"
    // So we add QLabel
    auto* projectNameLabel = new QLabel(tr("项目名称"), this);

    m_saveLocationEdit = new fluent::textfields::LineEdit(this);
    m_saveLocationEdit->setReadOnly(true);
    m_browseButton = new fluent::basicinput::Button(this);
    m_browseButton->setText(tr("浏览"));
    auto* saveLocationLabel = new QLabel(tr("保存位置"), this);
    auto* saveLocationLayout = new QHBoxLayout;
    saveLocationLayout->addWidget(m_saveLocationEdit, 1);
    saveLocationLayout->addWidget(m_browseButton);

    auto* directionLabel = new QLabel(tr("阅读方向"), this);
    m_rtlRadio = new fluent::basicinput::RadioButton(tr("从右到左（日式漫画）"), this);
    m_ltrRadio = new fluent::basicinput::RadioButton(tr("从左到右"), this);
    auto* directionLayout = new QVBoxLayout;
    directionLayout->addWidget(m_rtlRadio);
    directionLayout->addWidget(m_ltrRadio);

    auto* sourceLangLabel = new QLabel(tr("源语言"), this);
    m_sourceLangCombo = new fluent::basicinput::ComboBox(this);
    auto* targetLangLabel = new QLabel(tr("目标语言"), this);
    m_targetLangCombo = new fluent::basicinput::ComboBox(this);

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
    m_cancelImportButton = new fluent::basicinput::Button(this);
    m_cancelImportButton->setText(tr("取消"));
    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(m_submitButton, 1);
    buttonsLayout->addWidget(m_cancelImportButton);

    settingsLayout->addWidget(projectNameLabel);
    settingsLayout->addWidget(m_projectNameEdit);
    settingsLayout->addWidget(saveLocationLabel);
    settingsLayout->addLayout(saveLocationLayout);
    settingsLayout->addWidget(directionLabel);
    settingsLayout->addLayout(directionLayout);
    settingsLayout->addWidget(sourceLangLabel);
    settingsLayout->addWidget(m_sourceLangCombo);
    settingsLayout->addWidget(targetLangLabel);
    settingsLayout->addWidget(m_targetLangCombo);
    settingsLayout->addStretch(1);
    settingsLayout->addLayout(buttonsLayout);

    m_mainLayout->addWidget(m_sourcePane, 1);
    m_mainLayout->addWidget(m_settingsPane);
}

void ImportPage::bindViewModel() {
    connect(m_viewModel, &ImportViewModel::stateChanged, this, &ImportPage::updateState);

    connect(m_dropZone, &FileDropZone::pathsDropped, m_viewModel, &ImportViewModel::addSourcePaths);

    connect(
        m_projectNameEdit, &fluent::textfields::LineEdit::textChanged, m_viewModel, &ImportViewModel::setProjectName);

    connect(m_browseButton, &fluent::basicinput::Button::clicked, m_viewModel, &ImportViewModel::requestBrowseLocation);
    connect(m_viewModel, &ImportViewModel::browseLocationRequested, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择保存位置"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        if (!dir.isEmpty()) {
            m_viewModel->setSaveLocation(dir);
        }
    });

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

void ImportPage::updateState(const manga::ui::screen::import_::ImportState& state) {
    if (m_projectNameEdit->text() != state.projectName) {
        m_projectNameEdit->setText(state.projectName);
    }
    if (m_saveLocationEdit->text() != state.saveLocation) {
        m_saveLocationEdit->setText(state.saveLocation);
    }

    if (state.readingDirection == domain::ReadingDirection::RightToLeft) {
        m_rtlRadio->setChecked(true);
    } else {
        m_ltrRadio->setChecked(true);
    }

    auto setComboByData = [](fluent::basicinput::ComboBox* combo, const QString& data) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == data) {
                combo->setCurrentIndex(i);
                break;
            }
        }
    };
    setComboByData(m_sourceLangCombo, state.sourceLanguageCode);
    setComboByData(m_targetLangCombo, state.targetLanguageCode);

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
        m_cancelImportButton->setVisible(true);
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
    }
}

void ImportPage::updateResponsiveLayout(int availableWidth) {
    if (availableWidth >= 1008) {
        m_mainLayout->setDirection(QBoxLayout::LeftToRight);
        m_settingsPane->setFixedWidth(380);
    } else {
        m_mainLayout->setDirection(QBoxLayout::TopToBottom);
        m_settingsPane->setFixedWidth(QWIDGETSIZE_MAX);
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
