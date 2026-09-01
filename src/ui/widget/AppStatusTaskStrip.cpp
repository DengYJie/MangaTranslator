#include "ui/widget/AppStatusTaskStrip.h"

#include "ui/delegate/OperationRowDelegate.h"
#include "ui/model/OperationListModel.h"

#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Foundation.h>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace manga::ui::widget {

namespace {
constexpr int kIdleHeight = 24;
constexpr int kActiveHeight = 32;
constexpr int kHorizontalPadding = 12;
constexpr int kSectionSpacing = 16;
constexpr int kIconSize = 16;
constexpr int kMenuBtnWidth = 28;
} // namespace

AppStatusTaskStrip::AppStatusTaskStrip(QWidget* parent)
    : QWidget(parent), m_operationModel(new model::OperationListModel(this)) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(kIdleHeight);
    setAccessibleName(tr("应用状态与后台任务栏"));
    setToolTip(tr("显示文档保存状态与后台处理任务"));

    m_completionTimer = new QTimer(this);
    m_completionTimer->setSingleShot(true);
    connect(m_completionTimer, &QTimer::timeout, this, [this]() {
        m_hasCompletionSummary = false;
        m_completionText.clear();
        updateResponsiveLayout();
        update();
    });
}

AppStatusTaskStrip::~AppStatusTaskStrip() {
    closeTaskFlyout();
}

QSize AppStatusTaskStrip::sizeHint() const {
    const int fontHeight = fontMetrics().height();
    const int contentHeight = hasVisibleStatusContent() ? kActiveHeight : kIdleHeight;
    return {800, qMax(contentHeight, fontHeight + 10)};
}

QSize AppStatusTaskStrip::minimumSizeHint() const {
    return {100, kIdleHeight};
}

void AppStatusTaskStrip::onThemeUpdated() {
    update();
}

void AppStatusTaskStrip::setOperations(const QVector<application::OperationSnapshot>& operations) {
    checkTaskCompletionTransitions(operations);
    m_operationModel->setOperations(operations);
    updateActiveTaskProjection();
    updateGeometry();
    updateResponsiveLayout();
    update();
}

void AppStatusTaskStrip::setDocumentStatus(const DocumentStatusSnapshot& docStatus) {
    if (m_docStatus == docStatus) {
        return;
    }
    m_docStatus = docStatus;
    updateGeometry();
    updateResponsiveLayout();
    update();
}

void AppStatusTaskStrip::setEnvironmentStatus(const EnvironmentStatusSnapshot& envStatus) {
    if (m_envStatus == envStatus) {
        return;
    }
    m_envStatus = envStatus;
    updateGeometry();
    updateResponsiveLayout();
    update();
}

void AppStatusTaskStrip::checkTaskCompletionTransitions(const QVector<application::OperationSnapshot>& newOperations) {
    bool hadActiveTask = (m_activeTaskCount > 0);
    int newActiveCount = 0;
    bool justCompleted = false;

    for (const auto& op : newOperations) {
        if (op.state == application::OperationState::Running || op.state == application::OperationState::Queued ||
            op.state == application::OperationState::Cancelling) {
            newActiveCount++;
        } else if (op.state == application::OperationState::Completed) {
            justCompleted = true;
        }
    }

    if (hadActiveTask && newActiveCount == 0 && justCompleted) {
        m_hasCompletionSummary = true;
        m_completionText = tr("已完成全部任务");
        m_completionTimer->start(1500); // 1.5 seconds
    } else if (newActiveCount > 0) {
        m_hasCompletionSummary = false;
        m_completionTimer->stop();
    }
}

void AppStatusTaskStrip::updateActiveTaskProjection() {
    m_primaryActiveTask = std::nullopt;
    m_activeTaskCount = 0;

    for (const auto& op : m_operationModel->operations()) {
        if (op.state == application::OperationState::Running || op.state == application::OperationState::Queued ||
            op.state == application::OperationState::Cancelling) {
            m_activeTaskCount++;
            if (!m_primaryActiveTask.has_value() ||
                (op.state == application::OperationState::Running &&
                 m_primaryActiveTask->state != application::OperationState::Running)) {
                m_primaryActiveTask = op;
            }
        }
    }
}

AppStatusTaskStrip::Section AppStatusTaskStrip::highestPrioritySection() const {
    if (m_docStatus.status == DocumentSaveStatus::SaveFailed || m_docStatus.status == DocumentSaveStatus::Dirty) {
        return Section::Document;
    }
    if (m_activeTaskCount > 0 || m_hasCompletionSummary) {
        return Section::ActiveTask;
    }
    if (m_envStatus.condition == EnvironmentCondition::Offline ||
        m_envStatus.condition == EnvironmentCondition::ModelMissing ||
        m_envStatus.condition == EnvironmentCondition::MemoryPressure) {
        return Section::Environment;
    }
    if (m_docStatus.status == DocumentSaveStatus::Saved || m_docStatus.status == DocumentSaveStatus::Saving ||
        m_docStatus.status == DocumentSaveStatus::ReadOnly) {
        return Section::Document;
    }
    return Section::Environment;
}

QString AppStatusTaskStrip::activeTaskSummaryText() const {
    if (m_hasCompletionSummary) {
        return m_completionText;
    }
    if (!m_primaryActiveTask.has_value()) {
        return QString();
    }

    const auto& op = *m_primaryActiveTask;
    const QString target = op.descriptor.displayTarget;
    const QString stage = model::OperationListModel::stageName(op.descriptor.stage);
    const QString state = model::OperationListModel::stateName(op.state);
    const int progress = (op.total > 0) ? static_cast<int>(op.completed * 100 / op.total) : 0;

    QString text;
    if (op.state == application::OperationState::Running && progress > 0) {
        text = target.isEmpty() ? tr("%1 · %2 · %3%").arg(stage, state).arg(progress)
                                : tr("%1 · %2 · %3%").arg(target, stage).arg(progress);
    } else {
        text = target.isEmpty() ? tr("%1 · %2").arg(stage, state) : tr("%1 · %2 · %3").arg(target, stage, state);
    }

    if (m_activeTaskCount > 1) {
        text += tr(" 另有 %1 个任务").arg(m_activeTaskCount - 1);
    }

    return text;
}

void AppStatusTaskStrip::settleCompletionSummaryForTest() {
    if (m_completionTimer->isActive()) {
        m_completionTimer->stop();
    }
    m_hasCompletionSummary = false;
    m_completionText.clear();
    updateGeometry();
    updateResponsiveLayout();
    update();
}

bool AppStatusTaskStrip::hasVisibleStatusContent() const {
    return m_docStatus.status != DocumentSaveStatus::NoDocument || m_activeTaskCount > 0 || m_hasCompletionSummary ||
           m_envStatus.condition != EnvironmentCondition::Normal;
}

void AppStatusTaskStrip::triggerActiveTaskClickForTest() {
    if (m_primaryActiveTask.has_value()) {
        Q_EMIT operationActivated(
            m_primaryActiveTask->id, m_primaryActiveTask->descriptor.projectId, m_primaryActiveTask->descriptor.pageId);
    }
}

void AppStatusTaskStrip::triggerCancelTaskForTest(const application::OperationId& id) {
    Q_EMIT cancelRequested(id);
}

void AppStatusTaskStrip::triggerDocumentStatusClickForTest() {
    if (m_docStatus.status == DocumentSaveStatus::SaveFailed) {
        Q_EMIT saveFailureDetailsRequested();
    }
}

void AppStatusTaskStrip::openTaskFlyout() {
    if (!m_taskFlyout.isNull()) {
        closeTaskFlyout();
        return;
    }

    auto* flyout = new fluent::dialogs_flyouts::Flyout(this);
    flyout->setAttribute(Qt::WA_DeleteOnClose);
    connect(flyout, &QObject::destroyed, this, [this]() { m_taskFlyout = nullptr; });

    auto* layout = new QVBoxLayout(flyout);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(tr("后台任务 (%1)").arg(m_operationModel->rowCount()), flyout);
    QFont titleFont = titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    auto updateTitle = [this, titleLabel]() {
        titleLabel->setText(tr("后台任务 (%1)").arg(m_operationModel->rowCount()));
    };
    connect(m_operationModel, &QAbstractItemModel::rowsInserted, flyout, updateTitle);
    connect(m_operationModel, &QAbstractItemModel::rowsRemoved, flyout, updateTitle);
    connect(m_operationModel, &QAbstractItemModel::modelReset, flyout, updateTitle);

    auto* listView = new QListView(flyout);
    listView->setModel(m_operationModel);
    auto* delegate = new delegate::OperationRowDelegate(this, listView);
    listView->setItemDelegate(delegate);
    listView->setSelectionMode(QAbstractItemView::SingleSelection);
    listView->setMinimumWidth(320);
    listView->setMinimumHeight(180);
    listView->setMaximumHeight(300);

    connect(delegate, &delegate::OperationRowDelegate::cancelClicked, this, &AppStatusTaskStrip::cancelRequested);
    connect(listView, &QListView::clicked, this, [this, flyout](const QModelIndex& index) {
        const auto id = index.data(model::OperationListModel::IdRole).value<application::OperationId>();
        const auto projectId = index.data(model::OperationListModel::ProjectIdRole).value<domain::ProjectId>();
        const auto pageId = index.data(model::OperationListModel::PageIdRole).value<domain::PageId>();
        Q_EMIT operationActivated(id, projectId, pageId);
        flyout->close();
    });

    layout->addWidget(listView);

    flyout->showAt(this);
    m_taskFlyout = flyout;
}

void AppStatusTaskStrip::closeTaskFlyout() {
    if (!m_taskFlyout.isNull()) {
        m_taskFlyout->close();
        m_taskFlyout = nullptr;
    }
}

void AppStatusTaskStrip::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void AppStatusTaskStrip::updateResponsiveLayout() {
    const int w = width();
    const int h = height();
    const int menuX = w - kHorizontalPadding - kMenuBtnWidth;
    m_menuBtnHotspot = QRect(menuX, (h - kIconSize) / 2 - 4, kMenuBtnWidth, kIconSize + 8);

    if (w >= 1008) {
        // Wide: 3 full sections
        const int docW = 200;
        const int envW = 160;
        const int taskX = kHorizontalPadding + docW + kSectionSpacing;
        const int taskW = menuX - taskX - envW - kSectionSpacing;

        m_docHotspot = QRect(kHorizontalPadding, 0, docW, h);
        m_taskHotspot = QRect(taskX, 0, qMax(0, taskW), h);
        m_envHotspot = QRect(menuX - envW, 0, envW, h);
    } else if (w >= 720) {
        // Medium: Document left, Task center, Environment compact right
        const int docW = 180;
        const int envW = 32;
        const int taskX = kHorizontalPadding + docW + kSectionSpacing;
        const int taskW = menuX - taskX - envW - kSectionSpacing;

        m_docHotspot = QRect(kHorizontalPadding, 0, docW, h);
        m_taskHotspot = QRect(taskX, 0, qMax(0, taskW), h);
        m_envHotspot = QRect(menuX - envW, 0, envW, h);
    } else {
        // Narrow: Highest priority section only + menu button
        const int mainW = menuX - kHorizontalPadding - 8;
        const Section section = highestPrioritySection();
        m_docHotspot = (section == Section::Document) ? QRect(kHorizontalPadding, 0, mainW, h) : QRect();
        m_taskHotspot = (section == Section::ActiveTask) ? QRect(kHorizontalPadding, 0, mainW, h) : QRect();
        m_envHotspot = (section == Section::Environment) ? QRect(kHorizontalPadding, 0, mainW, h) : QRect();
    }
}

void AppStatusTaskStrip::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const fluent::FluentElement::Colors tokens = themeColorsRef();

    // Background & top border
    painter.fillRect(rect(), tokens.bgLayerAlt);
    painter.setPen(tokens.strokeCard);
    painter.drawLine(0, 0, width(), 0);

    QFont iconFont(Typography::FontFamily::FluentIcons);
    iconFont.setPixelSize(kIconSize);

    // 1. Draw Document Status
    if (!m_docHotspot.isEmpty() && m_docStatus.status != DocumentSaveStatus::NoDocument) {
        QString glyph;
        QColor color = tokens.textSecondary;
        QString text = m_docStatus.message;

        switch (m_docStatus.status) {
            case DocumentSaveStatus::Saved:
                glyph = QStringLiteral("\uE73E"); // Checkmark
                color = tokens.systemSuccess;
                if (text.isEmpty())
                    text = tr("所有更改已保存");
                break;
            case DocumentSaveStatus::Dirty:
                glyph = QStringLiteral("\uE70F"); // Edit / Dot
                color = tokens.textSecondary;
                if (text.isEmpty())
                    text = tr("有未保存更改");
                break;
            case DocumentSaveStatus::Saving:
                glyph = QStringLiteral("\uE712"); // Progress / Refresh
                color = tokens.accentDefault;
                if (text.isEmpty())
                    text = tr("正在保存");
                break;
            case DocumentSaveStatus::SaveFailed:
                glyph = QStringLiteral("\uE7BA"); // Warning / Alert
                color = tokens.systemCritical;
                if (text.isEmpty())
                    text = tr("保存失败");
                break;
            case DocumentSaveStatus::ReadOnly:
                glyph = QStringLiteral("\uE72E"); // Lock
                color = tokens.textSecondary;
                if (text.isEmpty())
                    text = tr("只读");
                break;
            default:
                break;
        }

        const QRect iconRect(m_docHotspot.left(), (height() - kIconSize) / 2, kIconSize, kIconSize);
        painter.setFont(iconFont);
        painter.setPen(color);
        painter.drawText(iconRect, Qt::AlignCenter, glyph);

        const QRect textRect(iconRect.right() + 6, 0, m_docHotspot.width() - kIconSize - 6, height());
        painter.setFont(font());
        painter.setPen(color);
        painter.drawText(textRect,
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));
    }

    // 2. Draw Active Task
    if (!m_taskHotspot.isEmpty()) {
        const QString summary = activeTaskSummaryText();
        if (!summary.isEmpty()) {
            const QRect iconRect(m_taskHotspot.left(), (height() - kIconSize) / 2, kIconSize, kIconSize);
            painter.setFont(iconFont);

            if (m_hasCompletionSummary) {
                painter.setPen(tokens.systemSuccess);
                painter.drawText(iconRect, Qt::AlignCenter, QStringLiteral("\uE73E"));
            } else {
                painter.setPen(tokens.accentDefault);
                painter.drawText(iconRect, Qt::AlignCenter, QStringLiteral("\uE768")); // Work
            }

            const QRect textRect(iconRect.right() + 6, 0, m_taskHotspot.width() - kIconSize - 6, height());
            painter.setFont(font());
            painter.setPen(m_hasCompletionSummary ? tokens.systemSuccess : tokens.textPrimary);
            painter.drawText(textRect,
                             Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(summary, Qt::ElideRight, textRect.width()));
        }
    }

    // 3. Draw Environment Status
    if (!m_envHotspot.isEmpty() && m_envStatus.condition != EnvironmentCondition::Normal) {
        QString glyph;
        QColor color = tokens.textSecondary;
        QString text = m_envStatus.statusText;

        switch (m_envStatus.condition) {
            case EnvironmentCondition::GpuAvailable:
                glyph = QStringLiteral("\uE7F4"); // Processing / Device
                if (text.isEmpty())
                    text = tr("GPU 可用");
                break;
            case EnvironmentCondition::CpuMode:
                glyph = QStringLiteral("\uE7F8");
                if (text.isEmpty())
                    text = tr("CPU 模式");
                break;
            case EnvironmentCondition::Offline:
                glyph = QStringLiteral("\uEB55"); // Offline / Disconnect
                color = tokens.systemCaution;
                if (text.isEmpty())
                    text = tr("离线");
                break;
            case EnvironmentCondition::ModelMissing:
                glyph = QStringLiteral("\uE7BA"); // Alert
                color = tokens.systemCritical;
                if (text.isEmpty())
                    text = tr("模型缺失");
                break;
            case EnvironmentCondition::MemoryPressure:
                glyph = QStringLiteral("\uE7BA");
                color = tokens.systemCaution;
                if (text.isEmpty())
                    text = tr("内存压力");
                break;
            default:
                break;
        }

        const QRect iconRect(m_envHotspot.left(), (height() - kIconSize) / 2, kIconSize, kIconSize);
        painter.setFont(iconFont);
        painter.setPen(color);
        painter.drawText(iconRect, Qt::AlignCenter, glyph);

        if (width() >= 1008 || (width() < 720 && highestPrioritySection() == Section::Environment)) {
            const QRect textRect(iconRect.right() + 6, 0, m_envHotspot.width() - kIconSize - 6, height());
            painter.setFont(font());
            painter.setPen(color);
            painter.drawText(textRect,
                             Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));
        }
    }

    // 4. Draw Flyout Menu Button on Far Right
    painter.setFont(iconFont);
    painter.setPen(m_activeTaskCount > 0 ? tokens.accentDefault : tokens.textSecondary);
    painter.drawText(m_menuBtnHotspot, Qt::AlignCenter, QStringLiteral("\uE8B7")); // Task list / activities
}

void AppStatusTaskStrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint pos = event->pos();

    if (m_menuBtnHotspot.contains(pos)) {
        openTaskFlyout();
        return;
    }

    if (!m_docHotspot.isEmpty() && m_docHotspot.contains(pos)) {
        if (m_docStatus.status == DocumentSaveStatus::SaveFailed) {
            Q_EMIT saveFailureDetailsRequested();
        }
        return;
    }

    if (!m_taskHotspot.isEmpty() && m_taskHotspot.contains(pos)) {
        if (m_primaryActiveTask.has_value()) {
            Q_EMIT operationActivated(m_primaryActiveTask->id,
                                      m_primaryActiveTask->descriptor.projectId,
                                      m_primaryActiveTask->descriptor.pageId);
        } else {
            openTaskFlyout();
        }
        return;
    }

    QWidget::mousePressEvent(event);
}

} // namespace manga::ui::widget
