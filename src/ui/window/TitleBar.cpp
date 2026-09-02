#include "ui/window/TitleBar.h"

#include "ui/animation/AnimatedBackVisualSource.h"
#include "ui/animation/AnimatedGlobalNavVisualSource.h"
#include "ui/animation/AnimatedIcon.h"

#include <FluentQt/Design.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <QAccessible>
#include <QApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>

namespace manga::ui::window {
namespace {
// WinUI 3 standard title bar height.
constexpr int TitleBarStandardHeight = 32;
// Tall height when hosting a search box or user card.
constexpr int TitleBarTallHeight = 48;
// Full-bleed backplate caption button width.
constexpr int CaptionButtonWidth = 46;
// Left-side navigation button width.
constexpr int AppTitleBarButtonWidth = 40;

// Left padding column width (Col 0).
constexpr int TitleBarLeftPaddingWidth = 2;
// Default left title padding column width (Col 4 DefaultSpacing).
constexpr int TitleBarLeftHeaderDefaultPaddingWidth = 14;
// Single-button negative inset padding column width (Col 4 NegativeInsetSpacing).
constexpr int TitleBarLeftHeaderNegativeInsetPaddingWidth = 2;
// Icon right margin.
constexpr int TitleBarIconMarginRight = 16;
// Title text right margin.
constexpr int TitleBarTitleMarginRight = 8;
// Subtitle text right margin.
constexpr int TitleBarSubtitleMarginRight = 16;
// Minimum guaranteed drag buffer (Col 10 Min Drag Region).
constexpr int TitleBarMinDragRegionWidth = 48;
// Gap kept between adjacent title bar content elements.
constexpr int ElementSpacing = 16;
// Standard icon pixel size.
constexpr int IconSize = 16;
// Content opacity while the window is inactive.
constexpr qreal InactiveOpacity = 0.5;
} // namespace

// Label with automatic trailing text elision; derives from the Fluent standard Label.
class ElidedLabel : public fluent::textfields::Label {
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : fluent::textfields::Label(parent) {
        setTextElideMode(Qt::ElideRight);
        setFluentTypography(Typography::FontRole::Caption);
    }

    void setContentOpacity(qreal opacity) {
        if (qFuzzyCompare(m_opacity, opacity))
            return;
        m_opacity = opacity;
        updateOpacityColor();
    }

    qreal contentOpacity() const {
        return m_opacity;
    }

protected:
    void onThemeUpdated() override {
        fluent::textfields::Label::onThemeUpdated();
        updateOpacityColor();
    }

private:
    void updateOpacityColor() {
        QColor textColor = themeColorsRef().textPrimary;
        textColor.setAlphaF(textColor.alphaF() * m_opacity);
        QPalette p = palette();
        p.setColor(QPalette::WindowText, textColor);
        setPalette(p);
    }

    qreal m_opacity = 1.0;
};

TitleBar::TitleBar(QWidget* parent) : fluent::windowing::TitleBar(parent) {
    setTitleBarHeight(TitleBarStandardHeight);

    m_backButton = new fluent::basicinput::Button(this);
    m_backButton->setObjectName(QStringLiteral("fluentWindowBackButton"));
    m_backButton->setFluentStyle(fluent::basicinput::Button::Subtle);
    m_backButton->setFluentLayout(fluent::basicinput::Button::IconOnly);
    m_backButton->setFocusPolicy(Qt::TabFocus);
    m_backButton->setFocusVisual(false);
    m_backButton->installEventFilter(this);
    m_backButton->hide();
    fluent::status_info::ToolTip::attach(m_backButton, tr("Back"));
    m_backButton->setAccessibleName(tr("Back"));
    connect(m_backButton, &fluent::basicinput::Button::clicked, this, &TitleBar::backButtonClicked);
    m_animatedBackIcon = new animation::AnimatedIcon(m_backButton);
    m_animatedBackIcon->setSource(std::make_shared<animation::AnimatedBackVisualSource>());

    m_paneToggleButton = new fluent::basicinput::Button(this);
    m_paneToggleButton->setObjectName(QStringLiteral("fluentWindowPaneToggleButton"));
    m_paneToggleButton->setFluentStyle(fluent::basicinput::Button::Subtle);
    m_paneToggleButton->setFluentLayout(fluent::basicinput::Button::IconOnly);
    m_paneToggleButton->setFocusPolicy(Qt::TabFocus);
    m_paneToggleButton->setFocusVisual(false);
    m_paneToggleButton->installEventFilter(this);
    m_paneToggleButton->hide();
    fluent::status_info::ToolTip::attach(m_paneToggleButton, tr("Menu"));
    m_paneToggleButton->setAccessibleName(tr("Menu"));
    connect(m_paneToggleButton, &fluent::basicinput::Button::clicked, this, &TitleBar::paneToggleButtonClicked);
    m_animatedPaneToggleIcon = new animation::AnimatedIcon(m_paneToggleButton);
    m_animatedPaneToggleIcon->setSource(std::make_shared<animation::AnimatedGlobalNavVisualSource>());

    m_iconButton = new fluent::basicinput::Button(this);
    m_iconButton->setObjectName(QStringLiteral("fluentWindowIconButton"));
    m_iconButton->setFluentStyle(fluent::basicinput::Button::Subtle);
    m_iconButton->setFluentLayout(fluent::basicinput::Button::IconOnly);
    m_iconButton->setFocusPolicy(Qt::NoFocus);
    m_iconButton->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_iconButton->hide();

    // Mouse events pass through so the title area keeps the system drag and
    // right-click window menu behavior.
    m_titleLabel = new ElidedLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("fluentWindowTitleLabel"));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setFont(themeFont(Typography::FontRole::Caption).toQFont());
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_subtitleLabel = new ElidedLabel(this);
    m_subtitleLabel->setObjectName(QStringLiteral("fluentWindowSubtitleLabel"));
    m_subtitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_subtitleLabel->setFont(themeFont(Typography::FontRole::Caption).toQFont());
    m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_subtitleLabel->hide();

    m_minimizeButton = createCaptionButton(
        QStringLiteral("fluentWindowMinimizeButton"), Typography::Icons::ChromeMinimize, tr("Minimize"));
    m_maximizeButton = createCaptionButton(
        QStringLiteral("fluentWindowMaximizeButton"), Typography::Icons::ChromeMaximize, tr("Maximize"));
    m_closeButton = createCaptionButton(
        QStringLiteral("fluentWindowCloseButton"), Typography::Icons::ChromeClose, tr("Close"), true);

    m_minimizeButton->installEventFilter(this);
    m_maximizeButton->installEventFilter(this);
    m_closeButton->installEventFilter(this);

    connect(m_minimizeButton, &fluent::basicinput::Button::clicked, window(), &QWidget::showMinimized);
    connect(m_maximizeButton, &fluent::basicinput::Button::clicked, window(), [this]() {
        if (window()->isMaximized()) {
            window()->showNormal();
        } else {
            window()->showMaximized();
        }
    });
    connect(m_closeButton, &fluent::basicinput::Button::clicked, window(), &QWidget::close);

    connect(window(), &QWidget::windowIconChanged, this, [this](const QIcon& icon) { setIcon(icon); });
    connect(window(), &QWidget::windowTitleChanged, this, [this](const QString& title) { setTitle(title); });

    connect(this, &fluent::windowing::TitleBar::titleBarHeightChanged, this, [this](int) {
        updateCaptionButtonSizes();
        updateLayout();
    });

    connect(this, &fluent::windowing::TitleBar::windowActiveChanged, this, [this](bool active) {
        syncActivationOpacity();
    });
}

TitleBar::~TitleBar() = default;

fluent::basicinput::Button* TitleBar::createCaptionButton(const QString& objectName,
                                                          const QString& glyph,
                                                          const QString& tooltip,
                                                          bool isCloseBtn) {
    auto* button = new fluent::basicinput::Button(this);
    button->setObjectName(objectName);
    fluent::status_info::ToolTip::attach(button, tooltip);
    button->setAccessibleName(tooltip);
    button->setFluentStyle(fluent::basicinput::Button::Subtle);
    button->setFluentLayout(fluent::basicinput::Button::IconOnly);
    button->setIconGlyph(glyph, IconSize);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCornerRadii(QMargins(0, 0, 0, 0));
    if (isCloseBtn) {
        button->setCriticalOnHover(true);
    }
    return button;
}

void TitleBar::setTitle(const QString& title) {
    if (m_title == title)
        return;
    m_title = title;
    m_titleLabel->setText(title);
    setAccessibleName(title);
    QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&nameEvent);
    updateLayout();
    Q_EMIT titleChanged(title);
}

QString TitleBar::title() const {
    return m_title;
}

void TitleBar::setSubtitle(const QString& subtitle) {
    if (m_subtitle == subtitle)
        return;
    m_subtitle = subtitle;
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
    setAccessibleDescription(subtitle);
    QAccessibleEvent descEvent(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&descEvent);
    updateLayout();
    Q_EMIT subtitleChanged(subtitle);
}

QString TitleBar::subtitle() const {
    return m_subtitle;
}

void TitleBar::setIcon(const QIcon& icon) {
    m_icon = icon;
    const bool isVisible = !m_icon.isNull();
    if (m_icon.isNull()) {
        m_iconButton->hide();
    } else {
        m_iconButton->setIcon(icon);
        m_iconButton->setIconSize(QSize(IconSize, IconSize));
        m_iconButton->show();
    }
    updateLayout();
    Q_EMIT iconChanged(icon);
    Q_EMIT hitTestWidgetChanged(m_iconButton, isVisible);
}

QIcon TitleBar::icon() const {
    return m_icon;
}

void TitleBar::setBackButtonVisible(bool visible) {
    if (m_backButtonVisible == visible)
        return;
    m_backButtonVisible = visible;
    m_backButton->setVisible(visible);
    updateLayout();
    Q_EMIT backButtonVisibleChanged(visible);
    Q_EMIT hitTestWidgetChanged(m_backButton, visible);
}

bool TitleBar::isBackButtonVisible() const {
    return m_backButtonVisible;
}

void TitleBar::setBackButtonEnabled(bool enabled) {
    if (m_backButtonEnabled == enabled)
        return;
    m_backButtonEnabled = enabled;
    m_backButton->setEnabled(enabled);
    Q_EMIT backButtonEnabledChanged(enabled);
}

bool TitleBar::isBackButtonEnabled() const {
    return m_backButtonEnabled;
}

void TitleBar::setPaneToggleButtonVisible(bool visible) {
    if (m_paneToggleButtonVisible == visible)
        return;
    m_paneToggleButtonVisible = visible;
    m_paneToggleButton->setVisible(visible);
    updateLayout();
    Q_EMIT paneToggleButtonVisibleChanged(visible);
    Q_EMIT hitTestWidgetChanged(m_paneToggleButton, visible);
}

bool TitleBar::isPaneToggleButtonVisible() const {
    return m_paneToggleButtonVisible;
}

void TitleBar::setHeightOption(HeightOption option) {
    if (m_heightOption == option)
        return;
    m_heightOption = option;
    updateHeight();
    Q_EMIT heightOptionChanged(option);
}

TitleBar::HeightOption TitleBar::heightOption() const {
    return m_heightOption;
}

void TitleBar::setContentWidget(QWidget* contentWidget, ContentAlignment alignment) {
    if (m_contentWidget == contentWidget && m_contentAlignment == alignment)
        return;
    if (m_contentWidget && m_contentWidget != contentWidget) {
        Q_EMIT hitTestWidgetChanged(m_contentWidget, false);
        m_contentWidget->removeEventFilter(this);
        m_contentWidget->setParent(nullptr);
    }
    m_contentWidget = contentWidget;
    m_contentAlignment = alignment;
    if (m_contentWidget) {
        m_contentWidget->setParent(this);
        m_contentWidget->installEventFilter(this);
        m_contentWidget->show();
        Q_EMIT hitTestWidgetChanged(m_contentWidget, true);
    }
    updateHeight();
    updateLayout();
}

QWidget* TitleBar::contentWidget() const {
    return m_contentWidget;
}

TitleBar::ContentAlignment TitleBar::contentAlignment() const {
    return m_contentAlignment;
}

void TitleBar::setRightHeaderWidget(QWidget* rightHeaderWidget) {
    if (m_rightHeaderWidget == rightHeaderWidget)
        return;
    if (m_rightHeaderWidget && m_rightHeaderWidget != rightHeaderWidget) {
        Q_EMIT hitTestWidgetChanged(m_rightHeaderWidget, false);
        m_rightHeaderWidget->setParent(nullptr);
    }
    m_rightHeaderWidget = rightHeaderWidget;
    if (m_rightHeaderWidget) {
        m_rightHeaderWidget->setParent(this);
        m_rightHeaderWidget->installEventFilter(this);
        m_rightHeaderWidget->show();
        Q_EMIT hitTestWidgetChanged(m_rightHeaderWidget, true);
    }
    updateHeight();
    updateLayout();
}

QWidget* TitleBar::rightHeaderWidget() const {
    return m_rightHeaderWidget;
}

void TitleBar::setLeftHeaderWidget(QWidget* leftHeaderWidget) {
    if (m_leftHeaderWidget == leftHeaderWidget)
        return;
    if (m_leftHeaderWidget && m_leftHeaderWidget != leftHeaderWidget) {
        Q_EMIT hitTestWidgetChanged(m_leftHeaderWidget, false);
        m_leftHeaderWidget->setParent(nullptr);
    }
    m_leftHeaderWidget = leftHeaderWidget;
    if (m_leftHeaderWidget) {
        m_leftHeaderWidget->setParent(this);
        m_leftHeaderWidget->installEventFilter(this);
        m_leftHeaderWidget->show();
        Q_EMIT hitTestWidgetChanged(m_leftHeaderWidget, true);
    }
    updateLayout();
}

QWidget* TitleBar::leftHeaderWidget() const {
    return m_leftHeaderWidget;
}

void TitleBar::setMenuBar(QWidget* menuBar) {
    if (m_menuBar == menuBar)
        return;
    if (m_menuBar && m_menuBar != menuBar) {
        Q_EMIT hitTestWidgetChanged(m_menuBar, false);
        m_menuBar->removeEventFilter(this);
        m_menuBar->setParent(nullptr);
    }
    m_menuBar = menuBar;
    if (m_menuBar) {
        m_menuBar->setParent(this);
        m_menuBar->installEventFilter(this);
        Q_EMIT hitTestWidgetChanged(m_menuBar, m_menuBar->isVisibleTo(this));
    }
    updateLayout();
}

QWidget* TitleBar::menuBar() const {
    return m_menuBar;
}

void TitleBar::setMinDragRegionWidth(int width) {
    int clampedWidth = qMax(0, width);
    if (m_minDragRegionWidth == clampedWidth)
        return;
    m_minDragRegionWidth = clampedWidth;
    updateLayout();
    Q_EMIT minDragRegionWidthChanged(m_minDragRegionWidth);
}

int TitleBar::minDragRegionWidth() const {
    return m_minDragRegionWidth;
}

void TitleBar::onThemeUpdated() {
    fluent::windowing::TitleBar::onThemeUpdated();

    QColor textColor = themeColorsRef().textPrimary;
    QPalette p = m_titleLabel->palette();
    p.setColor(QPalette::WindowText, textColor);
    m_titleLabel->setPalette(p);
    m_subtitleLabel->setPalette(p);

    m_titleLabel->setFont(themeFont(Typography::FontRole::Caption).toQFont());
    m_subtitleLabel->setFont(themeFont(Typography::FontRole::Caption).toQFont());

    updateCaptionButtonSizes();
    syncActivationOpacity();
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window() && event->type() == QEvent::WindowStateChange) {
        syncWindowState();
    } else if (watched == m_backButton || watched == m_paneToggleButton) {
        if (event->type() == QEvent::FocusIn) {
            auto* fe = static_cast<QFocusEvent*>(event);
            auto* btn = static_cast<fluent::basicinput::Button*>(watched);
            const bool isKeyboard = (fe->reason() == Qt::TabFocusReason || fe->reason() == Qt::BacktabFocusReason ||
                                     fe->reason() == Qt::ShortcutFocusReason);
            btn->setFocusVisual(isKeyboard);
        } else if (event->type() == QEvent::FocusOut) {
            auto* btn = static_cast<fluent::basicinput::Button*>(watched);
            btn->setFocusVisual(false);
        }
    } else if (watched == m_minimizeButton || watched == m_maximizeButton || watched == m_closeButton ||
               watched == m_leftHeaderWidget || watched == m_rightHeaderWidget || watched == m_contentWidget ||
               watched == m_menuBar) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::ShowToParent ||
            event->type() == QEvent::HideToParent || event->type() == QEvent::Resize) {
            if (watched == m_menuBar) {
                Q_EMIT hitTestWidgetChanged(m_menuBar, m_menuBar->isVisibleTo(this));
            }
            updateLayout();
        }
    }
    return fluent::windowing::TitleBar::eventFilter(watched, event);
}

void TitleBar::showEvent(QShowEvent* event) {
    fluent::windowing::TitleBar::showEvent(event);
    if (window()) {
        window()->installEventFilter(this);
        syncWindowState();
    }
}

void TitleBar::syncWindowState() {
    if (!window())
        return;
    const bool isMaximized = window()->isMaximized();
    m_maximizeButton->setIconGlyph(isMaximized ? Typography::Icons::ChromeRestore : Typography::Icons::ChromeMaximize,
                                   IconSize);
    m_maximizeButton->setAccessibleName(isMaximized ? tr("Restore") : tr("Maximize"));
    fluent::status_info::ToolTip::attach(m_maximizeButton, isMaximized ? tr("Restore") : tr("Maximize"));
    m_closeButton->setCornerRadii(QMargins(0, isMaximized ? 0 : themeRadius().control, 0, 0));
}

void TitleBar::resizeEvent(QResizeEvent* event) {
    fluent::windowing::TitleBar::resizeEvent(event);
    updateLayout();
}

void TitleBar::updateHeight() {
    bool requiresTall = (m_heightOption == HeightOption::Tall || m_rightHeaderWidget != nullptr);
    int targetHeight = requiresTall ? TitleBarTallHeight : TitleBarStandardHeight;
    setTitleBarHeight(targetHeight);
}

void TitleBar::updateCaptionButtonSizes() {
    const int h = titleBarHeight();
    m_backButton->setFixedSize(AppTitleBarButtonWidth - 4, h - 4);
    m_paneToggleButton->setFixedSize(AppTitleBarButtonWidth - 4, h - 4);
    m_iconButton->setFixedSize(IconSize, IconSize);
    m_minimizeButton->setFixedSize(CaptionButtonWidth, h);
    m_maximizeButton->setFixedSize(CaptionButtonWidth, h);
    m_closeButton->setFixedSize(CaptionButtonWidth, h);
}

void TitleBar::updateLayout() {
    const int w = width();
    const int h = titleBarHeight();

    int currentX = TitleBarLeftPaddingWidth;

    if (m_backButtonVisible) {
        m_backButton->setGeometry(currentX + 2, 2, AppTitleBarButtonWidth - 4, h - 4);
        currentX += AppTitleBarButtonWidth;
    }

    if (m_paneToggleButtonVisible) {
        m_paneToggleButton->setGeometry(currentX + 2, 2, AppTitleBarButtonWidth - 4, h - 4);
        currentX += AppTitleBarButtonWidth;
    }

    if (m_leftHeaderWidget && m_leftHeaderWidget->isVisibleTo(this)) {
        m_leftHeaderWidget->setGeometry(currentX,
                                        (h - m_leftHeaderWidget->height()) / 2,
                                        m_leftHeaderWidget->width(),
                                        m_leftHeaderWidget->height());
        currentX += m_leftHeaderWidget->width();
    }

    currentX += TitleBarLeftHeaderDefaultPaddingWidth;

    if (!m_icon.isNull()) {
        m_iconButton->setGeometry(currentX, (h - IconSize) / 2, IconSize, IconSize);
        currentX += IconSize + TitleBarIconMarginRight;
    }

    int rightOffset = w;

    if (m_closeButton && m_closeButton->isVisibleTo(this)) {
        rightOffset -= CaptionButtonWidth;
        m_closeButton->setGeometry(rightOffset, 0, CaptionButtonWidth, h);
    }
    if (m_maximizeButton && m_maximizeButton->isVisibleTo(this)) {
        rightOffset -= CaptionButtonWidth;
        m_maximizeButton->setGeometry(rightOffset, 0, CaptionButtonWidth, h);
    }
    if (m_minimizeButton && m_minimizeButton->isVisibleTo(this)) {
        rightOffset -= CaptionButtonWidth;
        m_minimizeButton->setGeometry(rightOffset, 0, CaptionButtonWidth, h);
    }

    rightOffset -= m_minDragRegionWidth;

    if (m_rightHeaderWidget && m_rightHeaderWidget->isVisibleTo(this)) {
        rightOffset -= m_rightHeaderWidget->width();
        m_rightHeaderWidget->setGeometry(rightOffset,
                                         (h - m_rightHeaderWidget->height()) / 2,
                                         m_rightHeaderWidget->width(),
                                         m_rightHeaderWidget->height());
        rightOffset -= ElementSpacing;
    }

    int contentLeft = w;
    if (m_contentWidget && m_contentWidget->isVisibleTo(this)) {
        if (m_contentAlignment == ContentAlignment::Center) {
            int sw = qMax(m_contentWidget->sizeHint().width(), m_contentWidget->width());
            int sh = qMax(m_contentWidget->sizeHint().height(), m_contentWidget->height());
            if (sh > h) {
                sh = h;
            }
            contentLeft = (w - sw) / 2;
            m_contentWidget->setGeometry(contentLeft, (h - sh) / 2, sw, sh);
        }
    }

    int titleMaxRight =
        qMin(rightOffset,
             (m_contentWidget && m_contentAlignment == ContentAlignment::Center) ? contentLeft - ElementSpacing
                                                                                 : rightOffset);

    int titleRequiredWidth = 0;
    if (!m_title.isEmpty()) {
        titleRequiredWidth +=
            m_titleLabel->fontMetrics().horizontalAdvance(m_titleLabel->text()) + 4 + TitleBarTitleMarginRight;
    }
    if (!m_subtitle.isEmpty()) {
        titleRequiredWidth +=
            m_subtitleLabel->fontMetrics().horizontalAdvance(m_subtitleLabel->text()) + 4 + TitleBarSubtitleMarginRight;
    }

    bool shouldCompact = false;
    if (m_contentWidget != nullptr) {
        if (!m_isCompact) {
            bool isSqueezed = false;
            if (m_contentAlignment == ContentAlignment::Center &&
                (currentX + titleRequiredWidth > contentLeft - ElementSpacing)) {
                isSqueezed = true;
            } else if (m_contentAlignment == ContentAlignment::Stretch) {
                int avail = titleMaxRight - currentX;
                if (avail < titleRequiredWidth + m_contentWidget->minimumSizeHint().width()) {
                    isSqueezed = true;
                }
            }
            if (isSqueezed) {
                m_compactModeThresholdWidth = w;
                m_isCompact = true;
                shouldCompact = true;
            }
        } else {
            if (w >= m_compactModeThresholdWidth) {
                m_compactModeThresholdWidth = 0;
                m_isCompact = false;
                shouldCompact = false;
            } else {
                shouldCompact = true;
            }
        }
    } else {
        m_isCompact = false;
    }

    if (shouldCompact) {
        m_titleLabel->hide();
        m_subtitleLabel->hide();
    } else {
        if (!m_title.isEmpty())
            m_titleLabel->show();
        if (!m_subtitle.isEmpty())
            m_subtitleLabel->show();

        if (!m_title.isEmpty()) {
            int titleWidth = m_titleLabel->fontMetrics().horizontalAdvance(m_titleLabel->text()) + 4;
            int maxAvailableTitleWidth = titleMaxRight - currentX;
            if (titleWidth > maxAvailableTitleWidth) {
                titleWidth = qMax(0, maxAvailableTitleWidth);
            }
            m_titleLabel->setGeometry(currentX, 0, titleWidth, h);
            currentX += titleWidth + TitleBarTitleMarginRight;
        }

        if (!m_subtitle.isEmpty()) {
            int subtitleWidth = m_subtitleLabel->fontMetrics().horizontalAdvance(m_subtitleLabel->text()) + 4;
            int maxAvailableSubtitleWidth = titleMaxRight - currentX;
            if (subtitleWidth > maxAvailableSubtitleWidth) {
                subtitleWidth = qMax(0, maxAvailableSubtitleWidth);
            }
            m_subtitleLabel->setGeometry(currentX, 0, subtitleWidth, h);
            currentX += subtitleWidth + TitleBarSubtitleMarginRight;
        }

        if (m_menuBar && m_menuBar->isVisibleTo(this)) {
            int mbWidth = m_menuBar->sizeHint().width();
            int maxAvailableMbWidth = titleMaxRight - currentX;
            if (mbWidth > maxAvailableMbWidth) {
                mbWidth = qMax(0, maxAvailableMbWidth);
            }
            int mbHeight = qMin(h, m_menuBar->sizeHint().height());
            if (mbHeight <= 0) {
                mbHeight = h;
            }
            m_menuBar->setGeometry(currentX, (h - mbHeight) / 2, mbWidth, mbHeight);
            currentX += mbWidth + ElementSpacing;
        }
    }

    if (m_contentWidget && m_contentAlignment == ContentAlignment::Stretch) {
        int stretchLeft = currentX;
        int stretchRight = rightOffset;
        int stretchWidth = qMax(0, stretchRight - stretchLeft);
        int sh = m_contentWidget->height();
        m_contentWidget->setGeometry(stretchLeft, (h - sh) / 2, stretchWidth, sh);
    }

    setSystemReservedTrailingWidth(3 * CaptionButtonWidth);
}

void TitleBar::syncActivationOpacity() {
    if (!window())
        return;
    const bool active = window()->isActiveWindow();
    const qreal opacity = active ? 1.0 : InactiveOpacity;

    m_backButton->setContentOpacity(opacity);
    m_paneToggleButton->setContentOpacity(opacity);
    m_iconButton->setContentOpacity(opacity);
    m_titleLabel->setContentOpacity(opacity);
    m_subtitleLabel->setContentOpacity(opacity);

    m_minimizeButton->setContentOpacity(opacity);
    m_maximizeButton->setContentOpacity(opacity);
    m_closeButton->setContentOpacity(opacity);
}

QList<QWidget*> TitleBar::hitTestVisibleWidgets() const {
    QList<QWidget*> widgets;
    if (m_backButton)
        widgets.append(m_backButton);
    if (m_paneToggleButton)
        widgets.append(m_paneToggleButton);
    if (m_iconButton)
        widgets.append(m_iconButton);
    if (m_leftHeaderWidget)
        widgets.append(m_leftHeaderWidget);
    if (m_menuBar && m_menuBar->isVisibleTo(this))
        widgets.append(m_menuBar);
    if (m_contentWidget)
        widgets.append(m_contentWidget);
    if (m_rightHeaderWidget)
        widgets.append(m_rightHeaderWidget);
    return widgets;
}

QWidget* TitleBar::systemButton(SystemButtonType type) const {
    switch (type) {
        case SystemButtonType::Minimize:
            return m_minimizeButton;
        case SystemButtonType::Maximize:
            return m_maximizeButton;
        case SystemButtonType::Close:
            return m_closeButton;
    }
    return nullptr;
}

void TitleBar::setSystemButtonVisible(SystemButtonType type, bool visible) {
    if (auto* btn = systemButton(type)) {
        if (btn->isVisibleTo(this) != visible) {
            btn->setVisible(visible);
            updateLayout();
            Q_EMIT hitTestWidgetChanged(btn, visible);
        }
    }
}

bool TitleBar::isSystemButtonVisible(SystemButtonType type) const {
    if (auto* btn = systemButton(type)) {
        return !btn->isHidden();
    }
    return false;
}
} // namespace manga::ui::window
