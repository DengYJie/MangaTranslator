#include "ui/screen/import/FileDropZone.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/TextFields.h>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QVBoxLayout>

namespace manga::ui::screen::import_ {

FileDropZone::FileDropZone(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setAccessibleName(tr("漫画文件拖放区，支持 JPG、PNG、WEBP 图片或图片文件夹"));
    setMinimumHeight(160);

    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    m_iconLabel = new fluent::textfields::Label(this);
    QFont iconFont(Typography::FontFamily::FluentIcons);
    iconFont.setPixelSize(32);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setText(QStringLiteral("\uE896"));
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_textLabel = new fluent::textfields::Label(this);
    m_textLabel->setFluentTypography(Typography::FontRole::BodyStrong);
    m_textLabel->setAlignment(Qt::AlignCenter);
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_formatLabel = new fluent::textfields::Label(tr("支持 JPG、JPEG、PNG、WEBP、图片文件夹"), this);
    m_formatLabel->setFluentTypography(Typography::FontRole::Caption);
    m_formatLabel->setAlignment(Qt::AlignCenter);
    m_formatLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_formatLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_browseFilesButton = new fluent::basicinput::Button(tr("浏览文件"), this);
    m_browseFilesButton->setObjectName(QStringLiteral("dropZoneBrowseFilesButton"));
    m_browseFilesButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_browseFilesButton->setToolTip(tr("选择漫画图像文件"));
    m_browseFilesButton->setAccessibleName(tr("浏览文件"));
    connect(m_browseFilesButton, &fluent::basicinput::Button::clicked, this, &FileDropZone::browseFilesRequested);

    m_browseFolderButton = new fluent::basicinput::Button(tr("选择文件夹"), this);
    m_browseFolderButton->setObjectName(QStringLiteral("dropZoneBrowseFolderButton"));
    m_browseFolderButton->setToolTip(tr("选择包含漫画图像的文件夹"));
    m_browseFolderButton->setAccessibleName(tr("选择文件夹"));
    connect(m_browseFolderButton, &fluent::basicinput::Button::clicked, this, &FileDropZone::browseFolderRequested);

    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->setAlignment(Qt::AlignCenter);
    buttonsLayout->setSpacing(8);
    buttonsLayout->addWidget(m_browseFilesButton);
    buttonsLayout->addWidget(m_browseFolderButton);

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel);
    layout->addWidget(m_formatLabel);
    layout->addLayout(buttonsLayout);

    onThemeUpdated();
    refreshText();
}

FileDropZone::~FileDropZone() = default;

void FileDropZone::onThemeUpdated() {
    update();
}

void FileDropZone::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        refreshText();
    }
}

void FileDropZone::refreshText() {
    if (!isEnabled()) {
        m_textLabel->setText(tr("导入进行中"));
        return;
    }
    if (m_visualState == VisualState::DragEnter) {
        m_textLabel->setText(tr("释放以添加"));
    } else if (m_visualState == VisualState::Rejected) {
        m_textLabel->setText(tr("不支持的文件类型"));
    } else {
        m_textLabel->setText(tr("拖放漫画文件或文件夹到这里"));
    }
}

void FileDropZone::dragEnterEvent(QDragEnterEvent* event) {
    if (!isEnabled()) {
        event->ignore();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        bool allSupported = true;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) {
                allSupported = false;
                break;
            }
            QString path = url.toLocalFile();
            QFileInfo info(path);
            QString ext = info.suffix().toLower();
            if (info.isFile()) {
                if (!ext.isEmpty() && ext != QStringLiteral("jpg") && ext != QStringLiteral("jpeg") &&
                    ext != QStringLiteral("png") && ext != QStringLiteral("webp")) {
                    allSupported = false;
                    break;
                }
            } else if (!info.isDir()) {
                if (!ext.isEmpty() && ext != QStringLiteral("jpg") && ext != QStringLiteral("jpeg") &&
                    ext != QStringLiteral("png") && ext != QStringLiteral("webp")) {
                    allSupported = false;
                    break;
                }
            }
        }

        if (allSupported) {
            event->acceptProposedAction();
            updateVisualState(VisualState::DragEnter);
        } else {
            event->ignore();
            updateVisualState(VisualState::Rejected);
        }
    } else {
        event->ignore();
    }
}

void FileDropZone::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    updateVisualState(VisualState::Rest);
}

void FileDropZone::dropEvent(QDropEvent* event) {
    QVector<QString> paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    updateVisualState(VisualState::Rest);
    Q_EMIT pathsDropped(paths);
}

void FileDropZone::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = themeColorsRef();

    QColor borderColor = colors.strokeDefault;
    QColor bgColor = Qt::transparent;
    Qt::PenStyle penStyle = Qt::DashLine;

    if (!isEnabled()) {
        borderColor = colors.strokeDefault;
    } else if (m_visualState == VisualState::DragEnter) {
        borderColor = colors.accentDefault;
        bgColor = borderColor;
        bgColor.setAlpha(30); // Accent low-alpha fill
        penStyle = Qt::SolidLine;
    } else if (m_visualState == VisualState::Rejected) {
        borderColor = colors.systemCritical;
        penStyle = Qt::SolidLine;
    }

    QPen pen(borderColor, 1, penStyle);
    painter.setPen(pen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
}

void FileDropZone::updateVisualState(VisualState state) {
    m_visualState = state;
    refreshText();
    update();
}

} // namespace manga::ui::screen::import_
