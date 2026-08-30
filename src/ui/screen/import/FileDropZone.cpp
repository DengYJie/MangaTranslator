#include "ui/screen/import/FileDropZone.h"

#include <FluentQt/FluentQt.h>
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

    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    m_iconLabel = new QLabel(this);
    QFont iconFont(Typography::FontFamily::FluentIcons);
    iconFont.setPixelSize(32);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setText(QStringLiteral("\uE8B5")); // Document icon
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_textLabel = new QLabel(tr("将文件拖放到此处"), this);
    m_textLabel->setAlignment(Qt::AlignCenter);
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel);

    updateVisualState(VisualState::Rest);
}

FileDropZone::~FileDropZone() = default;

void FileDropZone::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        bool allSupported = true;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) {
                allSupported = false;
                break;
            }
            QString path = url.toLocalFile();
            QFileInfo info(path);
            if (info.isFile()) {
                QString ext = info.suffix().toLower();
                if (ext != QStringLiteral("jpg") && ext != QStringLiteral("jpeg") && ext != QStringLiteral("png") &&
                    ext != QStringLiteral("webp")) {
                    allSupported = false;
                    break;
                }
            } else if (!info.isDir()) { // 无后缀等同于文件夹启发式在Task7，但这里检查后缀
                QString ext = info.suffix();
                if (!ext.isEmpty()) { // heuristic: folders might not have suffix, or they do but we let them pass if
                                      // isDir. If not isDir and not isFile?
                    // Actually let's just let folders pass, or files without suffix if they are assumed folders
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

    if (m_visualState == VisualState::DragEnter) {
        borderColor = colors.accentDefault;
        bgColor = borderColor;
        bgColor.setAlpha(30); // Accent 低透明背景
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
    if (state == VisualState::Rest) {
        m_textLabel->setText(tr("将文件拖放到此处"));
    } else if (state == VisualState::DragEnter) {
        m_textLabel->setText(tr("释放以添加"));
    } else if (state == VisualState::Rejected) {
        m_textLabel->setText(tr("不支持的文件类型"));
    }
    update();
}

} // namespace manga::ui::screen::import_
