#include "ui/widget/ActivityCenter.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#include <QDebug>
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
constexpr int kThrottleIntervalMs = 30;

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
            return QObject::tr("加载缩略图");
        case application::OperationStage::Project:
            return QObject::tr("全页智能翻译");
        default:
            return QObject::tr("后台处理");
    }
}

QString stageGlyph(application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Detect:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular"));
        case application::OperationStage::Ocr:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_type_20_regular"));
        case application::OperationStage::Translate:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular"));
        case application::OperationStage::Clear:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_eraser_20_regular"));
        case application::OperationStage::Layout:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_text_font_size_20_regular"));
        case application::OperationStage::Export:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_arrow_export_up_20_regular"));
        case application::OperationStage::Import:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_folder_open_20_regular"));
        case application::OperationStage::LoadImage:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_image_20_regular"));
        case application::OperationStage::Project:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_sparkle_20_regular"));
        default:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_sparkle_20_regular"));
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
        m_iconLabel = new fluent::textfields::Label(this);
        QFont iconFont(Typography::FontFamily::FluentIcons);
        iconFont.setPixelSize(16);
        m_iconLabel->setFont(iconFont);
        m_iconLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Accent);
        m_iconLabel->setFixedSize(20, 20);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        rootLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

        // Center info
        auto* centerLayout = new QVBoxLayout();
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(3);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(4);

        m_titleLabel = new fluent::textfields::Label(this);
        m_titleLabel->setFluentTypography(Typography::FontRole::Body);
        QFont titleFont = m_titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        m_titleLabel->setFont(titleFont);
        titleRow->addWidget(m_titleLabel, 1);

        m_percentLabel = new fluent::textfields::Label(this);
        m_percentLabel->setFluentTypography(Typography::FontRole::Caption);
        m_percentLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        m_percentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        titleRow->addWidget(m_percentLabel, 0);
        centerLayout->addLayout(titleRow);

        // Subtitle / statusText
        m_subLabel = new fluent::textfields::Label(this);
        m_subLabel->setFluentTypography(Typography::FontRole::Caption);
        m_subLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        centerLayout->addWidget(m_subLabel);

        // Progress bar
        m_progress = new MicroProgressBar(this);
        centerLayout->addWidget(m_progress);

        rootLayout->addLayout(centerLayout, 1);

        // Right cancel button
        m_cancelBtn = new fluent::basicinput::Button(this);
        m_cancelBtn->setFluentStyle(fluent::basicinput::Button::Subtle);
        m_cancelBtn->setFluentLayout(fluent::basicinput::Button::IconOnly);
        m_cancelBtn->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_dismiss_circle_20_regular")),
                                  Typography::IconSize::Compact);
        m_cancelBtn->setFixedSize(24, 24);
        m_cancelBtn->setAccessibleName(tr("取消任务"));
        m_cancelBtn->setToolTip(tr("取消任务"));
        connect(m_cancelBtn, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT cancelRequested(m_opId); });
        rootLayout->addWidget(m_cancelBtn, 0, Qt::AlignVCenter);

        updateFrom(op);
    }

    void updateFrom(const application::OperationSnapshot& op) {
        m_iconLabel->setText(stageGlyph(op.descriptor.stage));

        QString target = op.descriptor.displayTarget;
        if (target.isEmpty()) {
            target = op.descriptor.pageId.isValid() ? tr("当前页面") : tr("后台任务");
        }
        const QString title = QStringLiteral("%1 · %2").arg(target, stageName(op.descriptor.stage));
        if (m_titleLabel->text() != title) {
            m_titleLabel->setText(title);
        }

        const int percent = (op.total > 0 && op.completed >= 0)
                                ? std::clamp(static_cast<int>(op.completed * 100 / op.total), 0, 100)
                                : 0;

        const QString percentText = QStringLiteral("%1%").arg(percent);
        if (m_percentLabel->text() != percentText) {
            m_percentLabel->setText(percentText);
        }

        QString subText = op.statusText;
        if (subText.isEmpty()) {
            subText = (op.state == application::OperationState::Cancelling) ? tr("正在取消...") : tr("处理中...");
        }
        if (m_subLabel->text() != subText) {
            m_subLabel->setText(subText);
        }

        m_progress->setProgress(percent);

        const bool canCancel = (op.state != application::OperationState::Cancelling && op.descriptor.cancellable);
        if (m_cancelBtn->isEnabled() != canCancel) {
            m_cancelBtn->setEnabled(canCancel);
        }
    }

    void markCompleted() {
        m_percentLabel->setText(QStringLiteral("100%"));
        m_subLabel->setText(tr("已完成"));
        m_progress->setProgress(100);
        m_cancelBtn->setEnabled(false);
    }

    [[nodiscard]] const application::OperationId& operationId() const noexcept {
        return m_opId;
    }

    [[nodiscard]] QSize sizeHint() const override {
        return QSize(308, 62);
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(308, 62);
    }

Q_SIGNALS:
    void cancelRequested(const application::OperationId& id);

private:
    application::OperationId m_opId;
    fluent::textfields::Label* m_iconLabel{nullptr};
    fluent::textfields::Label* m_titleLabel{nullptr};
    fluent::textfields::Label* m_percentLabel{nullptr};
    fluent::textfields::Label* m_subLabel{nullptr};
    MicroProgressBar* m_progress{nullptr};
    fluent::basicinput::Button* m_cancelBtn{nullptr};
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
        iconLabel->setText(Typography::Icons::glyph(QStringLiteral("ic_fluent_error_circle_20_filled")));
        iconLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.systemCritical.name(QColor::HexRgb)));
        iconLabel->setFixedSize(20, 20);
        iconLabel->setAlignment(Qt::AlignCenter);
        rootLayout->addWidget(iconLabel, 0, Qt::AlignTop);

        // Center info
        auto* centerLayout = new QVBoxLayout();
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(2);

        m_titleLabel = new fluent::textfields::Label(this);
        m_titleLabel->setFluentTypography(Typography::FontRole::Body);
        QFont titleFont = m_titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        m_titleLabel->setFont(titleFont);
        m_titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tokens.systemCritical.name(QColor::HexRgb)));
        centerLayout->addWidget(m_titleLabel);

        m_msgLabel = new fluent::textfields::Label(this);
        m_msgLabel->setFluentTypography(Typography::FontRole::Caption);
        m_msgLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
        m_msgLabel->setWordWrap(true);
        centerLayout->addWidget(m_msgLabel);

        rootLayout->addLayout(centerLayout, 1);

        // Right dismiss button
        auto* dismissBtn = new fluent::basicinput::Button(this);
        dismissBtn->setFluentStyle(fluent::basicinput::Button::Subtle);
        dismissBtn->setFluentLayout(fluent::basicinput::Button::IconOnly);
        dismissBtn->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_dismiss_20_regular")),
                                 Typography::IconSize::Compact);
        dismissBtn->setFixedSize(24, 24);
        dismissBtn->setAccessibleName(tr("忽略"));
        dismissBtn->setToolTip(tr("忽略"));
        connect(dismissBtn, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT dismissRequested(m_opId); });
        rootLayout->addWidget(dismissBtn, 0, Qt::AlignVCenter);

        updateFrom(op);
    }

    void updateFrom(const application::OperationSnapshot& op) {
        QString target = op.descriptor.displayTarget;
        if (target.isEmpty()) {
            target = tr("第 1 页");
        }
        const QString title = QStringLiteral("%1 · %2 失败").arg(target, stageName(op.descriptor.stage));
        if (m_titleLabel->text() != title) {
            m_titleLabel->setText(title);
        }

        QString errorMsg = op.statusText;
        if (errorMsg.isEmpty() && op.error.has_value()) {
            errorMsg = op.error->safeMessage;
        }
        if (errorMsg.isEmpty()) {
            errorMsg = tr("处理发生错误");
        }
        if (m_msgLabel->text() != errorMsg) {
            m_msgLabel->setText(errorMsg);
        }
    }

    [[nodiscard]] const application::OperationId& operationId() const noexcept {
        return m_opId;
    }

    [[nodiscard]] QSize sizeHint() const override {
        return QSize(308, 66);
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return QSize(308, 66);
    }

Q_SIGNALS:
    void dismissRequested(const application::OperationId& id);

private:
    application::OperationId m_opId;
    fluent::textfields::Label* m_titleLabel{nullptr};
    fluent::textfields::Label* m_msgLabel{nullptr};
};

} // namespace

ActivityCenter::ActivityCenter(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("editorActivityCenter"));
    setFixedWidth(320);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
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
            clearRows();
        }
    });

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        if (m_visibleItemCount == 0) {
            startHideAnimation();
        }
    });

    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setSingleShot(true);
    connect(m_throttleTimer, &QTimer::timeout, this, [this]() { syncOperations(m_pendingOperations); });

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(6, 6, 6, 6);
    m_layout->setSpacing(4);
}

void ActivityCenter::setOperations(const QVector<application::OperationSnapshot>& operations) {
    m_pendingOperations = operations;
    if (!m_throttleTimer->isActive()) {
        m_throttleTimer->start(kThrottleIntervalMs);
    }
}

void ActivityCenter::clearRows() {
    for (auto* w : m_jobRows) {
        w->deleteLater();
    }
    m_jobRows.clear();

    for (auto* w : m_failureRows) {
        w->deleteLater();
    }
    m_failureRows.clear();

    for (auto* w : m_separators) {
        w->deleteLater();
    }
    m_separators.clear();

    QLayoutItem* item = nullptr;
    while ((item = m_layout->takeAt(0)) != nullptr) {
        delete item;
    }
}

void ActivityCenter::dismissFailedOperation(const application::OperationId& id) {
    m_dismissedFailedIds.insert(id);
    syncOperations(m_pendingOperations);
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

QSize ActivityCenter::sizeHint() const {
    int totalH = 12; // 6 top + 6 bottom margins
    if (m_layout != nullptr) {
        const int count = m_layout->count();
        for (int i = 0; i < count; ++i) {
            if (auto* item = m_layout->itemAt(i)) {
                if (auto* w = item->widget()) {
                    totalH += w->sizeHint().height();
                }
            }
        }
        if (count > 1) {
            totalH += (count - 1) * m_layout->spacing();
        }
    }
    return QSize(320, std::max(12, totalH));
}

void ActivityCenter::updateTargetPosition(const QSize& parentSize) {
    const int margin = 16;
    m_targetX = std::max(margin, parentSize.width() - width() - margin);
    m_targetY = std::max(margin, parentSize.height() - height() - margin);
}

void ActivityCenter::updateParentGeometry(const QSize& parentSize) {
    resize(sizeHint());
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
        resize(sizeHint());
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

void ActivityCenter::syncOperations(const QVector<application::OperationSnapshot>& operations) {
    QVector<application::OperationSnapshot> activeOps;
    QVector<application::OperationSnapshot> failedOps;

    for (const auto& op : operations) {
        // Filter silent background cache/I/O tasks (e.g. thumbnail, page image load, preview, import)
        if (op.descriptor.stage == application::OperationStage::LoadImage ||
            op.descriptor.stage == application::OperationStage::Import) {
            continue;
        }

        if (op.state == application::OperationState::Running || op.state == application::OperationState::Queued ||
            op.state == application::OperationState::Cancelling) {
            activeOps.append(op);
        } else if (op.state == application::OperationState::Failed && !m_dismissedFailedIds.contains(op.id)) {
            failedOps.append(op);
        }
    }

    const int totalVisible = activeOps.size() + failedOps.size();
    m_visibleItemCount = totalVisible;

    if (totalVisible == 0) {
        for (auto* w : m_jobRows) {
            auto* row = static_cast<ActivityJobRow*>(w);
            row->markCompleted();
        }

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

    bool structureChanged = false;

    // 1. Sync active job rows (in-place update or create/delete)
    for (auto it = m_jobRows.begin(); it != m_jobRows.end();) {
        auto* row = static_cast<ActivityJobRow*>(*it);
        bool stillActive = false;
        for (const auto& op : activeOps) {
            if (op.id == row->operationId()) {
                stillActive = true;
                break;
            }
        }
        if (!stillActive) {
            m_layout->removeWidget(row);
            row->deleteLater();
            it = m_jobRows.erase(it);
            structureChanged = true;
        } else {
            ++it;
        }
    }

    for (const auto& op : activeOps) {
        ActivityJobRow* existingRow = nullptr;
        for (auto* w : m_jobRows) {
            auto* row = static_cast<ActivityJobRow*>(w);
            if (row->operationId() == op.id) {
                existingRow = row;
                break;
            }
        }
        if (existingRow != nullptr) {
            existingRow->updateFrom(op);
        } else {
            auto* newRow = new ActivityJobRow(op, this);
            connect(newRow, &ActivityJobRow::cancelRequested, this, &ActivityCenter::cancelRequested);
            m_jobRows.push_back(newRow);
            structureChanged = true;
        }
    }

    // 2. Sync failed rows
    for (auto it = m_failureRows.begin(); it != m_failureRows.end();) {
        auto* row = static_cast<ActivityFailureRow*>(*it);
        bool stillFailed = false;
        for (const auto& op : failedOps) {
            if (op.id == row->operationId()) {
                stillFailed = true;
                break;
            }
        }
        if (!stillFailed) {
            m_layout->removeWidget(row);
            row->deleteLater();
            it = m_failureRows.erase(it);
            structureChanged = true;
        } else {
            ++it;
        }
    }

    for (const auto& op : failedOps) {
        ActivityFailureRow* existingRow = nullptr;
        for (auto* w : m_failureRows) {
            auto* row = static_cast<ActivityFailureRow*>(w);
            if (row->operationId() == op.id) {
                existingRow = row;
                break;
            }
        }
        if (existingRow != nullptr) {
            existingRow->updateFrom(op);
        } else {
            auto* newRow = new ActivityFailureRow(op, this);
            connect(newRow, &ActivityFailureRow::dismissRequested, this, [this](const application::OperationId& id) {
                dismissFailedOperation(id);
            });
            m_failureRows.push_back(newRow);
            structureChanged = true;
        }
    }

    // 3. Re-align layout if structure changed (widgets added/removed)
    if (structureChanged) {
        QLayoutItem* item = nullptr;
        while ((item = m_layout->takeAt(0)) != nullptr) {
            delete item;
        }
        for (auto* sep : m_separators) {
            sep->deleteLater();
        }
        m_separators.clear();

        bool first = true;
        for (auto* row : m_jobRows) {
            if (!first) {
                auto* sep = new QFrame(this);
                sep->setFrameShape(QFrame::HLine);
                sep->setFixedHeight(1);
                m_separators.push_back(sep);
                m_layout->addWidget(sep);
            }
            first = false;
            m_layout->addWidget(row);
        }
        for (auto* row : m_failureRows) {
            if (!first) {
                auto* sep = new QFrame(this);
                sep->setFrameShape(QFrame::HLine);
                sep->setFixedHeight(1);
                m_separators.push_back(sep);
                m_layout->addWidget(sep);
            }
            first = false;
            m_layout->addWidget(row);
        }

        m_layout->activate();
        resize(sizeHint());
        if (parentWidget() != nullptr) {
            updateTargetPosition(parentWidget()->size());
            move(m_targetX, m_targetY);
        }
    }

    if (!isVisible() || m_isHiding) {
        startShowAnimation();
    }
}

} // namespace manga::ui::widget

#include "ActivityCenter.moc"
