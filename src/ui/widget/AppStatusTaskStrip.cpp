#include "ui/widget/AppStatusTaskStrip.h"

#include "ui/model/OperationListModel.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Windowing.h>
#include <QEvent>
#include <QPainter>
#include <QTimer>

namespace manga::ui::widget {

namespace {
constexpr int kBarHeight = 24;
constexpr int kHorizontalPadding = 12;
} // namespace

AppStatusTaskStrip::AppStatusTaskStrip(QWidget* parent)
    : QWidget(parent), m_operationModel(new model::OperationListModel(this)) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kBarHeight);
    setFont(themeFont(Typography::FontRole::Caption).toQFont());
    setAccessibleName(tr("应用状态栏"));

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, &AppStatusTaskStrip::clearMessage);
}

AppStatusTaskStrip::~AppStatusTaskStrip() = default;

QSize AppStatusTaskStrip::sizeHint() const {
    return {800, kBarHeight};
}

QSize AppStatusTaskStrip::minimumSizeHint() const {
    return {100, kBarHeight};
}

void AppStatusTaskStrip::onThemeUpdated() {
    setFont(themeFont(Typography::FontRole::Caption).toQFont());
    update();
}

void AppStatusTaskStrip::showMessage(const QString& message, int timeoutMs) {
    m_message = message;
    if (timeoutMs > 0) {
        m_messageTimer->start(timeoutMs);
    } else {
        m_messageTimer->stop();
    }
    update();
}

void AppStatusTaskStrip::clearMessage() {
    if (m_message.isEmpty()) {
        return;
    }
    m_message.clear();
    m_messageTimer->stop();
    update();
}

void AppStatusTaskStrip::setOperations(const QVector<application::OperationSnapshot>& operations) {
    m_operationModel->setOperations(operations);
}

void AppStatusTaskStrip::setDocumentStatus(const DocumentStatusSnapshot& docStatus) {
    if (m_docStatus == docStatus) {
        return;
    }
    m_docStatus = docStatus;

    if (docStatus.status == DocumentSaveStatus::SaveFailed) {
        showMessage(docStatus.message.isEmpty() ? tr("保存失败") : docStatus.message, 3000);
    } else if (docStatus.status == DocumentSaveStatus::Saving) {
        showMessage(docStatus.message.isEmpty() ? tr("正在保存...") : docStatus.message, 3000);
    } else if (docStatus.status == DocumentSaveStatus::Saved && !docStatus.message.isEmpty()) {
        showMessage(docStatus.message, 3000);
    }
}

void AppStatusTaskStrip::setEnvironmentStatus(const EnvironmentStatusSnapshot& envStatus) {
    m_envStatus = envStatus;
}

bool AppStatusTaskStrip::event(QEvent* event) {
    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate) {
        update();
    }
    return QWidget::event(event);
}

void AppStatusTaskStrip::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const fluent::FluentElement::Colors tokens = themeColorsRef();

    const bool paintedMaterial = fluent::windowing::windowBackdropUsesPaintedMaterial(window());
    const QColor backdrop =
        paintedMaterial ? QColor() : fluent::windowing::windowChromeBackdropFill(*this, window(), isActiveWindow());

    if (paintedMaterial) {
        // Continuous software material
    } else if (backdrop.isValid()) {
        painter.fillRect(rect(), backdrop);
    } else if (fluent::windowing::windowBackdropRequiresTransparentClear(window())) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        painter.fillRect(rect(), tokens.bgLayerAlt);
    }

    // Clean top boundary separator line
    painter.setPen(tokens.strokeCard);
    painter.drawLine(0, 0, width(), 0);

    // Draw transient message if active (auto hides after 3s)
    if (!m_message.isEmpty()) {
        const QRect messageRect(kHorizontalPadding, 0, width() - (kHorizontalPadding * 2), height());
        painter.setFont(font());
        painter.setPen(tokens.textSecondary);
        painter.drawText(messageRect,
                         Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_message, Qt::ElideRight, messageRect.width()));
    }
}

} // namespace manga::ui::widget
