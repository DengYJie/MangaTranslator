#include "ui/delegate/OperationRowDelegate.h"

#include "ui/model/OperationListModel.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace manga::ui::delegate {

namespace {
constexpr int kRowHeight = 56;
constexpr int kIconSize = 20;
constexpr int kCancelBtnSize = 24;
constexpr int kCancelBtnPadding = 12;

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
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_play_20_regular"));
        default:
            return Typography::Icons::glyph(QStringLiteral("ic_fluent_play_20_regular"));
    }
}

} // namespace

OperationRowDelegate::OperationRowDelegate(fluent::FluentElement* themeSource, QObject* parent)
    : QStyledItemDelegate(parent), m_themeSource(themeSource) {}

QRect OperationRowDelegate::cancelButtonRect(const QRect& optionRect) {
    return QRect(optionRect.right() - kCancelBtnSize - kCancelBtnPadding,
                 optionRect.top() + (optionRect.height() - kCancelBtnSize) / 2,
                 kCancelBtnSize,
                 kCancelBtnSize);
}

QSize OperationRowDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(index);
    return {option.rect.width(), kRowHeight};
}

void OperationRowDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const fluent::FluentElement::Colors tokens =
        m_themeSource ? m_themeSource->themeColorsRef() : fluent::FluentElement::Colors{};

    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    if (selected) {
        painter->fillRect(option.rect, tokens.controlSecondary);
    } else if (hovered) {
        painter->fillRect(option.rect, tokens.subtleSecondary);
    }

    const auto stage = index.data(model::OperationListModel::StageRole).value<application::OperationStage>();
    const auto state = index.data(model::OperationListModel::StateRole).value<application::OperationState>();
    const QString displayTarget = index.data(model::OperationListModel::DisplayTargetRole).toString();
    const QString stageName = index.data(model::OperationListModel::StageNameRole).toString();
    const QString stateName = index.data(model::OperationListModel::StateNameRole).toString();
    const int progress = index.data(model::OperationListModel::ProgressPercentRole).toInt();
    const bool cancellable = index.data(model::OperationListModel::CancellableRole).toBool();
    const QString errorMsg = index.data(model::OperationListModel::ErrorMessageRole).toString();

    // 1. Stage icon on the left
    const QRect iconRect(
        option.rect.left() + 12, option.rect.top() + (option.rect.height() - kIconSize) / 2, kIconSize, kIconSize);
    QFont iconFont(Typography::FontFamily::FluentIcons);
    iconFont.setPixelSize(kIconSize);
    painter->setFont(iconFont);

    if (state == application::OperationState::Failed) {
        painter->setPen(tokens.systemCritical);
        painter->drawText(
            iconRect, Qt::AlignCenter, Typography::Icons::glyph(QStringLiteral("ic_fluent_error_circle_20_filled")));
    } else if (state == application::OperationState::Completed) {
        painter->setPen(tokens.systemSuccess);
        painter->drawText(iconRect,
                          Qt::AlignCenter,
                          Typography::Icons::glyph(QStringLiteral("ic_fluent_checkmark_circle_20_filled")));
    } else {
        painter->setPen(state == application::OperationState::Running ? tokens.accentDefault : tokens.textSecondary);
        painter->drawText(iconRect, Qt::AlignCenter, stageGlyph(stage));
    }

    // 2. Text layout
    const int textLeft = iconRect.right() + 12;
    const int textRight =
        cancellable ? (option.rect.right() - kCancelBtnSize - kCancelBtnPadding - 8) : (option.rect.right() - 12);
    const QRect textBounds(textLeft, option.rect.top() + 8, textRight - textLeft, option.rect.height() - 16);

    const QRect topTextRect(textBounds.left(), textBounds.top(), textBounds.width(), textBounds.height() / 2);
    const QRect bottomTextRect(
        textBounds.left(), topTextRect.bottom(), textBounds.width(), textBounds.height() - topTextRect.height());

    // Title: display target and stage name, or the stage name alone when there is no target.
    QFont titleFont = option.font;
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(tokens.textPrimary);
    const QString title = displayTarget.isEmpty() ? stageName : QStringLiteral("%1 · %2").arg(displayTarget, stageName);
    painter->drawText(topTextRect,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, topTextRect.width()));

    // Status subtitle
    QFont subFont = option.font;
    subFont.setPixelSize(11);
    painter->setFont(subFont);

    QString subText;
    if (state == application::OperationState::Running && progress > 0) {
        subText = QStringLiteral("%1 · %2%").arg(stateName).arg(progress);
        painter->setPen(tokens.accentDefault);
    } else if (state == application::OperationState::Failed) {
        subText = errorMsg.isEmpty() ? stateName : QStringLiteral("%1 · %2").arg(stateName, errorMsg);
        painter->setPen(tokens.systemCritical);
    } else if (state == application::OperationState::Completed) {
        subText = stateName;
        painter->setPen(tokens.systemSuccess);
    } else {
        subText = stateName;
        painter->setPen(tokens.textSecondary);
    }
    painter->drawText(bottomTextRect,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(subText, Qt::ElideRight, bottomTextRect.width()));

    // 3. Cancel button hotspot
    if (cancellable) {
        const QRect cancelRect = cancelButtonRect(option.rect);
        painter->setFont(iconFont);
        painter->setPen(tokens.textTertiary);
        painter->drawText(
            cancelRect, Qt::AlignCenter, Typography::Icons::glyph(QStringLiteral("ic_fluent_dismiss_20_regular")));
    }

    // 4. Progress bar line at bottom edge if running
    if (state == application::OperationState::Running && progress > 0) {
        const int progressWidth = option.rect.width() * progress / 100;
        painter->fillRect(QRect(option.rect.left(), option.rect.bottom() - 2, progressWidth, 2), tokens.accentDefault);
    }

    painter->restore();
}

bool OperationRowDelegate::editorEvent(QEvent* event,
                                       QAbstractItemModel* model,
                                       const QStyleOptionViewItem& option,
                                       const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            const bool cancellable = index.data(model::OperationListModel::CancellableRole).toBool();
            if (cancellable && cancelButtonRect(option.rect).contains(mouseEvent->pos())) {
                const auto id = index.data(model::OperationListModel::IdRole).value<application::OperationId>();
                Q_EMIT cancelClicked(id);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace manga::ui::delegate
