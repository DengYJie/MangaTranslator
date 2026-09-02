#include "ui/widget/ActivityCenter.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace manga::ui::widget {

namespace {

constexpr int kHideGraceMs = 600;
constexpr int kFastShowDurationMs = 120;
constexpr int kSlowHideDurationMs = 300;

QString stageName(application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Ocr:
            return QObject::tr("OCR 识别");
        case application::OperationStage::Translate:
            return QObject::tr("AI 翻译");
        case application::OperationStage::Clear:
            return QObject::tr("背景清除");
        case application::OperationStage::Layout:
            return QObject::tr("文字排版");
        case application::OperationStage::Detect:
            return QObject::tr("文本检测");
        case application::OperationStage::Export:
            return QObject::tr("导出页面");
        case application::OperationStage::Import:
            return QObject::tr("导入项目");
        case application::OperationStage::LoadImage:
            return QObject::tr("加载图片");
        case application::OperationStage::Project:
            return QObject::tr("全页智能翻译");
        default:
            return QObject::tr("后台处理");
    }
}

QString stageGlyph(application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Ocr:
        case application::OperationStage::Detect:
            return QStringLiteral("\uE80F"); // Document
        case application::OperationStage::Translate:
            return QStringLiteral("\uE774"); // Globe / Translate
        case application::OperationStage::Clear:
            return QStringLiteral("\uE74D"); // Eraser
        case application::OperationStage::Layout:
            return QStringLiteral("\uE8D3"); // Font
        case application::OperationStage::Export:
            return QStringLiteral("\uEDE1"); // Export
        case application::OperationStage::Import:
            return QStringLiteral("\uE8B5"); // Import
        case application::OperationStage::LoadImage:
            return QStringLiteral("\uEB9F"); // Photo
        default:
            return QStringLiteral("\uE768"); // Work
    }
}

class MicroProgressBar : public QWidget, public fluent::FluentElement {
public:
    explicit MicroProgressBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(4);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setProgress(int percent) {
        m_percent = std::clamp(percent, 0, 100);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto tokens = themeColorsRef();
        const QRectF r = rect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(tokens.controlSecondary);
        painter.drawRoundedRect(r, 2.0, 2.0);

        if (m_percent > 0) {
            QRectF fillRect = r;
            fillRect.setWidth(r.width() * m_percent / 100.0);
            painter.setBrush(tokens.accentDefault);
            painter.drawRoundedRect(fillRect, 2.0, 2.0);
        }
    }

private:
    int m_percent{0};
};

class ActivityJobRow : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    explicit ActivityJobRow(const application::OperationSnapshot& op, QWidget* parent = nullptr)
        : QWidget(parent), m_opId(op.id) {
        auto* rootLayout = new QHBoxLayout(this);
        rootLayout->setContentsMargins(10, 8, 10, 8);
        rootLayout->setSpacing(10);

        // Left stage icon
        auto* iconLabel = new fluent::textfields::Label(this);
        QFont iconFont(Typography::FontFamily::FluentIcons);
        iconFont.setPixelSize(16);
        iconLabel->setFont(iconFont);
        iconLabel->setText(stageGlyph(op.descriptor.stage));
        iconLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Accent);
        iconLabel->setFixedSize(20, 20);
        iconLabel->setAlignment(Qt::AlignCenter);
        rootLayout->addWidget(iconLabel, 0, Qt::AlignTop);

        // Center info
        auto* centerLayout = new QVBoxLayout();
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(3);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(4);

        QString target = op.descriptor.displayTarget;
        if (target.isEmpty()) {
            target = op.descriptor.pageId.isValid() ? tr("当前页面") : tr("后台任务");
        }
        auto* titleLabel =
            new fluent::textfields::Label(QStringLiteral("%1 · %2").arg(target, stageName(op.descriptor.stage)), this);
        titleLabel->setFluentTypography(Typography::FontRole::Body);
        QFont titleFont = titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        titleLabel->setFont(titleFont);
        titleRow->addWidget(titleLabel, 1);

        const int percent = (op.total > 0 && op.completed >= 0)
                                ? std::clamp(static_cast<int>(op.completed * 100 / op.total), 0, 100)
                                : 0;

        auto* percentLabel = new fluent::textfields::Label(QStringLiteral("%1%").arg(percent), this);
        percentLabel->setFluentTypography(Typography::FontRole::Caption);
        percentLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        percentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        titleRow->addWidget(percentLabel, 0);
        centerLayout->addLayout(titleRow);

        // Subtitle / statusText
        QString subText = op.statusText;
        if (subText.isEmpty()) {
            subText = (op.state == application::OperationState::Cancelling) ? tr("正在取消...") : tr("处理中...");
        }
        auto* subLabel = new fluent::textfields::Label(subText, this);
        subLabel->setFluentTypography(Typography::FontRole::Caption);
        subLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        centerLayout->addWidget(subLabel);

        // Progress bar
        auto* progress = new MicroProgressBar(this);
        progress->setProgress(percent);
        centerLayout->addWidget(progress);

        rootLayout->addLayout(centerLayout, 1);

        // Right cancel button
        auto* cancelBtn = new fluent::basicinput::Button(this);
        cancelBtn->setFluentStyle(fluent::basicinput::Button::Subtle);
        cancelBtn->setFluentLayout(fluent::basicinput::Button::IconOnly);
        cancelBtn->setIconGlyph(QStringLiteral("\uE71A"), Typography::IconSize::Compact); // Stop square
        cancelBtn->setFixedSize(24, 24);
        cancelBtn->setAccessibleName(tr("取消任务"));
        cancelBtn->setToolTip(tr("取消任务"));
        if (op.state == application::OperationState::Cancelling || !op.descriptor.cancellable) {
            cancelBtn->setEnabled(false);
        } else {
            connect(
                cancelBtn, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT cancelRequested(m_opId); });
        }
        rootLayout->addWidget(cancelBtn, 0, Qt::AlignVCenter);
    }

Q_SIGNALS:
    void cancelRequested(const application::OperationId& id);

private:
    application::OperationId m_opId;
};

class ActivityFailureRow : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    explicit ActivityFailureRow(const application::OperationSnapshot& op, QWidget* parent = nullptr)
        : QWidget(parent), m_opId(op.id) {
        auto* rootLayout = new QHBoxLayout(this);
        rootLayout->setContentsMargins(10, 8, 10, 8);
        rootLayout->setSpacing(10);

        const auto tokens = themeColorsRef();

        // Left error icon
        auto* iconLabel = new fluent::textfields::Label(this);
        QFont iconFont(Typography::FontFamily::FluentIcons);
        iconFont.setPixelSize(16);
        iconLabel->setFont(iconFont);
        iconLabel->setText(QStringLiteral("\uE7BA")); // Warning
        iconLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.systemCritical.name(QColor::HexRgb)));
        iconLabel->setFixedSize(20, 20);
        iconLabel->setAlignment(Qt::AlignCenter);
        rootLayout->addWidget(iconLabel, 0, Qt::AlignTop);

        // Center info
        auto* centerLayout = new QVBoxLayout();
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(2);

        QString target = op.descriptor.displayTarget;
        if (target.isEmpty()) {
            target = tr("第 1 页");
        }
        auto* titleLabel = new fluent::textfields::Label(
            QStringLiteral("%1 · %2 失败").arg(target, stageName(op.descriptor.stage)), this);
        titleLabel->setFluentTypography(Typography::FontRole::Body);
        QFont titleFont = titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        titleLabel->setFont(titleFont);
        titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.systemCritical.name(QColor::HexRgb)));
        centerLayout->addWidget(titleLabel);

        QString errorMsg = op.statusText;
        if (errorMsg.isEmpty() && op.error.has_value()) {
            errorMsg = op.error->safeMessage;
        }
        if (errorMsg.isEmpty()) {
            errorMsg = tr("处理发生错误");
        }
        auto* msgLabel = new fluent::textfields::Label(errorMsg, this);
        msgLabel->setFluentTypography(Typography::FontRole::Caption);
        msgLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        msgLabel->setWordWrap(true);
        centerLayout->addWidget(msgLabel);

        rootLayout->addLayout(centerLayout, 1);

        // Right dismiss button
        auto* dismissBtn = new fluent::basicinput::Button(this);
        dismissBtn->setFluentStyle(fluent::basicinput::Button::Subtle);
        dismissBtn->setFluentLayout(fluent::basicinput::Button::IconOnly);
        dismissBtn->setIconGlyph(QStringLiteral("\uE711"), Typography::IconSize::Compact); // Close X
        dismissBtn->setFixedSize(24, 24);
        dismissBtn->setAccessibleName(tr("忽略"));
        dismissBtn->setToolTip(tr("忽略"));
        connect(dismissBtn, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT dismissRequested(m_opId); });
        rootLayout->addWidget(dismissBtn, 0, Qt::AlignVCenter);
    }

Q_SIGNALS:
    void dismissRequested(const application::OperationId& id);

private:
    application::OperationId m_opId;
};

} // namespace

ActivityCenter::ActivityCenter(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("editorActivityCenter"));
    setFixedWidth(320);
    setAttribute(Qt::WA_StyledBackground, false);
    setVisible(false);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    m_opacityEffect->setOpacity(1.0);

    m_opacityAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            setVisible(false);
            m_isHiding = false;
        }
    });

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        if (m_visibleItemCount == 0) {
            startHideAnimation();
        }
    });

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(6, 6, 6, 6);
    m_layout->setSpacing(4);
}

void ActivityCenter::setOperations(const QVector<application::OperationSnapshot>& operations) {
    rebuildItems(operations);
}

void ActivityCenter::dismissFailedOperation(const application::OperationId& id) {
    m_dismissedFailedIds.insert(id);
    update();
}

void ActivityCenter::clearAllDismissed() {
    m_dismissedFailedIds.clear();
}

void ActivityCenter::onThemeUpdated() {
    update();
}

void ActivityCenter::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto tokens = themeColorsRef();
    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    // Fluent Elevated Card background with subtle border and rounded corners
    painter.setPen(tokens.strokeCard);
    painter.setBrush(tokens.bgLayerAlt);
    painter.drawRoundedRect(r, 12.0, 12.0);
}

void ActivityCenter::updateTargetPosition(const QSize& parentSize) {
    const int margin = 16;
    m_targetX = std::max(margin, parentSize.width() - width() - margin);
    m_targetY = std::max(margin, parentSize.height() - height() - margin);
}

void ActivityCenter::updateParentGeometry(const QSize& parentSize) {
    updateTargetPosition(parentSize);
    move(m_targetX, m_targetY);
    if (isVisible()) {
        raise();
    }
}

void ActivityCenter::startShowAnimation() {
    m_hideTimer->stop();
    m_isHiding = false;
    m_opacityAnim->stop();

    if (parentWidget() != nullptr) {
        updateTargetPosition(parentWidget()->size());
        move(m_targetX, m_targetY);
    }

    if (!isVisible()) {
        m_opacityEffect->setOpacity(0.0);
        setVisible(true);
        raise();
    } else {
        raise();
    }

    // Fast fade in
    if (m_opacityEffect->opacity() < 0.99) {
        m_opacityAnim->setDuration(kFastShowDurationMs);
        m_opacityAnim->setStartValue(m_opacityEffect->opacity());
        m_opacityAnim->setEndValue(1.0);
        m_opacityAnim->setEasingCurve(QEasingCurve::OutQuad);
        m_opacityAnim->start();
    } else {
        m_opacityEffect->setOpacity(1.0);
    }
}

void ActivityCenter::startHideAnimation() {
    m_hideTimer->stop();

    if (!isVisible() || m_isHiding) {
        return;
    }

    m_isHiding = true;
    m_opacityAnim->stop();

    // Slow fade out
    m_opacityAnim->setDuration(kSlowHideDurationMs);
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::InQuad);
    m_opacityAnim->start();
}

void ActivityCenter::rebuildItems(const QVector<application::OperationSnapshot>& operations) {
    // Clear previous item widgets
    QLayoutItem* child = nullptr;
    while ((child = m_layout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    QVector<application::OperationSnapshot> activeOps;
    QVector<application::OperationSnapshot> failedOps;

    for (const auto& op : operations) {
        if (op.state == application::OperationState::Running || op.state == application::OperationState::Queued ||
            op.state == application::OperationState::Cancelling) {
            activeOps.append(op);
        } else if (op.state == application::OperationState::Failed && !m_dismissedFailedIds.contains(op.id)) {
            failedOps.append(op);
        }
    }

    m_visibleItemCount = activeOps.size() + failedOps.size();

    // If no active or failed operations, wait a short grace period then fade out
    if (m_visibleItemCount == 0) {
        if (isVisible() && !m_isHiding) {
            if (!m_hideTimer->isActive()) {
                m_hideTimer->start(kHideGraceMs);
            }
        } else {
            m_hideTimer->stop();
        }
        return;
    }

    m_hideTimer->stop();

    bool first = true;
    for (const auto& op : activeOps) {
        if (!first) {
            auto* sep = new QFrame(this);
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            m_layout->addWidget(sep);
        }
        first = false;
        auto* row = new ActivityJobRow(op, this);
        connect(row, &ActivityJobRow::cancelRequested, this, &ActivityCenter::cancelRequested);
        m_layout->addWidget(row);
    }

    for (const auto& op : failedOps) {
        if (!first) {
            auto* sep = new QFrame(this);
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            m_layout->addWidget(sep);
        }
        first = false;
        auto* row = new ActivityFailureRow(op, this);
        connect(row, &ActivityFailureRow::dismissRequested, this, [this](const application::OperationId& id) {
            dismissFailedOperation(id);
            sender()->deleteLater();
            m_visibleItemCount = std::max(0, m_visibleItemCount - 1);
            if (m_visibleItemCount == 0) {
                startHideAnimation();
            } else {
                adjustSize();
                if (parentWidget() != nullptr) {
                    updateTargetPosition(parentWidget()->size());
                    move(m_targetX, m_targetY);
                }
            }
        });
        m_layout->addWidget(row);
    }

    adjustSize();
    if (parentWidget() != nullptr) {
        updateTargetPosition(parentWidget()->size());
        move(m_targetX, m_targetY);
    }

    startShowAnimation();
}

} // namespace manga::ui::widget

#include "ActivityCenter.moc"
