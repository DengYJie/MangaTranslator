#include "WindowBase.h"

#include "TitleBar.h"

#include <FluentQt/StatusInfo.h>
#include <QColor>
#include <QDebug>
#include <QHBoxLayout>
#include <QMargins>
#include <QPainter>
#include <QStyle>
#include <QWKWidgets/widgetwindowagent.h>

namespace {
void registerBackdropMetaTypes() {
    static const bool registered = [] {
        qRegisterMetaType<fluent::windowing::BackdropEffect>("fluent::windowing::BackdropEffect");
        qRegisterMetaType<fluent::windowing::BackdropState>("fluent::windowing::BackdropState");
        return true;
    }();
    Q_UNUSED(registered);
}
} // namespace

namespace manga::ui::window {

class WindowBasePrivate {
    Q_DECLARE_PUBLIC(WindowBase)

public:
    explicit WindowBasePrivate(WindowBase* q) : q_ptr(q) {}

    void setupUi();

    void setupWindowKit();

    bool applyPlatformWindowAttributes();

    void applyBackdropEffect();

    WindowBase* q_ptr;

    QVBoxLayout* m_rootLayout = nullptr;
    TitleBar* m_titleBar = nullptr;
    QWidget* m_contentHost = nullptr;
    QWidget* m_contentWidget = nullptr;

#if defined(Q_OS_WIN)
    fluent::windowing::BackdropEffect m_backdropEffect = fluent::windowing::BackdropEffect::Mica;
#else
    fluent::windowing::BackdropEffect m_backdropEffect = fluent::windowing::BackdropEffect::Solid;
#endif
    bool m_customWindowChromeEnabled = true;

    QWK::WidgetWindowAgent* m_windowAgent = nullptr;
};

void WindowBasePrivate::setupUi() {
    Q_Q(WindowBase);

    m_rootLayout = new QVBoxLayout(q);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // 1. Title bar
    m_titleBar = new TitleBar(q);
    m_rootLayout->addWidget(m_titleBar);

    // 2. Content area
    m_contentHost = new QWidget(q);
    auto* contentLayout = new QVBoxLayout(m_contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    m_rootLayout->addWidget(m_contentHost, 1);
}

void WindowBasePrivate::setupWindowKit() {
    Q_Q(WindowBase);

    if (m_windowAgent)
        return;

    m_windowAgent = new QWK::WidgetWindowAgent(q);
    m_windowAgent->setup(q);

    m_windowAgent->setTitleBar(m_titleBar);

    if (auto* minimizeBtn = m_titleBar->systemButton(TitleBar::SystemButtonType::Minimize)) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, minimizeBtn);
    }
    if (auto* maximizeBtn = m_titleBar->systemButton(TitleBar::SystemButtonType::Maximize)) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, maximizeBtn);
    }
    if (auto* closeBtn = m_titleBar->systemButton(TitleBar::SystemButtonType::Close)) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, closeBtn);
    }

    const auto hitWidgets = m_titleBar->hitTestVisibleWidgets();
    for (QWidget* w : hitWidgets) {
        m_windowAgent->setHitTestVisible(w, true);
    }

    QObject::connect(m_titleBar, &TitleBar::hitTestWidgetChanged, q, [this](QWidget* w, bool visible) {
        if (m_windowAgent && w) {
            m_windowAgent->setHitTestVisible(w, visible);
        }
    });

    applyBackdropEffect();
}

bool WindowBasePrivate::applyPlatformWindowAttributes() {
    Q_Q(WindowBase);
    if (!m_windowAgent)
        return false;

    const bool isDark = (q->effectiveTheme() == fluent::FluentElement::Dark);

#if defined(Q_OS_WIN)
    m_windowAgent->setWindowAttribute(QStringLiteral("dark-mode"), isDark);

    switch (m_backdropEffect) {
        case fluent::windowing::BackdropEffect::Solid:
            break;
        case fluent::windowing::BackdropEffect::Mica:
            return m_windowAgent->setWindowAttribute(QStringLiteral("mica"), true);
        case fluent::windowing::BackdropEffect::Acrylic:
            return m_windowAgent->setWindowAttribute(QStringLiteral("acrylic-material"), true);
        default:
            return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    m_windowAgent->setWindowAttribute(QStringLiteral("no-system-buttons"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("blur-effect"),
                                      isDark ? QStringLiteral("dark") : QStringLiteral("light"));
    m_windowAgent->setWindowAttribute(QStringLiteral("glass-effect"), QStringLiteral("regular"));
    m_windowAgent->setWindowAttribute(QStringLiteral("glass-corner-radius"), 24.0);
    m_windowAgent->setWindowAttribute(QStringLiteral("glass-tint-color"), QColor(255, 255, 255, 46));
    return true;
#endif
    return false;
}

void WindowBasePrivate::applyBackdropEffect() {
    Q_Q(WindowBase);

    const bool isTranslucent = (m_backdropEffect != fluent::windowing::BackdropEffect::Solid);

    fluent::windowing::BackdropState state;
    state.requestedEffect = m_backdropEffect;
    state.effectiveEffect = m_backdropEffect;
    state.backend = isTranslucent ? fluent::windowing::BackdropBackend::DwmSystemBackdrop
                                  : fluent::windowing::BackdropBackend::Solid;
    state.surfaceMode = isTranslucent ? fluent::windowing::BackdropSurfaceMode::CompositedTransparent
                                      : fluent::windowing::BackdropSurfaceMode::SolidOpaque;
    state.platformApplied = false;

    if (m_windowAgent && q->testAttribute(Qt::WA_WState_Polished)) {
        bool applied = applyPlatformWindowAttributes();
        if (!applied && isTranslucent) {
            qDebug() << "[WindowBase] Failed to apply translucent backdrop. Falling back to Solid.";
            m_backdropEffect = fluent::windowing::BackdropEffect::Solid;
            state.effectiveEffect = fluent::windowing::BackdropEffect::Solid;
            state.backend = fluent::windowing::BackdropBackend::Solid;
            state.surfaceMode = fluent::windowing::BackdropSurfaceMode::SolidOpaque;
        }
    }

    state.platformApplied = true;

    q->setProperty("fluentWindowBackdropEffect", static_cast<int>(state.requestedEffect));
    q->setProperty("fluentMicaBackdrop",
                   state.surfaceMode == fluent::windowing::BackdropSurfaceMode::CompositedTransparent);
    q->setProperty("fluentBackdropSurfaceMode", static_cast<int>(state.surfaceMode));
    q->setProperty("fluentBackdropBackend", static_cast<int>(state.backend));
    fluent::windowing::publishWindowBackdropState(q, state);
    q->update();
}

WindowBase::WindowBase(QWidget* parent) : QWidget(parent), d_ptr(std::make_unique<WindowBasePrivate>(this)) {
    Q_D(WindowBase);

    registerBackdropMetaTypes();

    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_TranslucentBackground, true);

    d->setupUi();
    d->setupWindowKit();

    WindowBase::onThemeUpdated();
}

WindowBase::~WindowBase() = default;

QWidget* WindowBase::contentWidget() const {
    Q_D(const WindowBase);
    return d->m_contentWidget;
}

void WindowBase::setContentWidget(QWidget* widget) {
    Q_D(WindowBase);
    if (d->m_contentWidget == widget)
        return;

    if (d->m_contentWidget) {
        d->m_contentWidget->deleteLater();
    }

    d->m_contentWidget = widget;

    if (d->m_contentWidget) {
        d->m_contentHost->layout()->addWidget(d->m_contentWidget);
    }

    Q_EMIT contentWidgetChanged();
}

TitleBar* WindowBase::titleBar() const {
    Q_D(const WindowBase);
    return d->m_titleBar;
}

QWidget* WindowBase::contentHost() const {
    Q_D(const WindowBase);
    return d->m_contentHost;
}

void WindowBase::setBackdropEffect(fluent::windowing::BackdropEffect effect) {
    Q_D(WindowBase);
    if (d->m_backdropEffect == effect)
        return;

    d->m_backdropEffect = effect;
    d->applyBackdropEffect();

    Q_EMIT backdropEffectChanged();
}

fluent::windowing::BackdropEffect WindowBase::backdropEffect() const {
    Q_D(const WindowBase);
    return d->m_backdropEffect;
}

void WindowBase::setCustomWindowChromeEnabled(bool enabled) {
    Q_D(WindowBase);
    if (d->m_customWindowChromeEnabled == enabled)
        return;

    d->m_customWindowChromeEnabled = enabled;

    if (d->m_titleBar) {
        d->m_titleBar->setVisible(enabled);
    }

    Q_EMIT customWindowChromeEnabledChanged();
}

bool WindowBase::customWindowChromeEnabled() const {
    Q_D(const WindowBase);
    return d->m_customWindowChromeEnabled;
}

void WindowBase::onThemeUpdated() {
    Q_D(WindowBase);
    if (d->m_windowAgent) {
        d->applyPlatformWindowAttributes();
    }

    if (d->m_titleBar) {
        d->m_titleBar->onThemeUpdated();
    }

    update();
}

void WindowBase::setHitTestVisible(QWidget* widget, bool visible) {
    Q_D(WindowBase);
    if (d->m_windowAgent && widget) {
        d->m_windowAgent->setHitTestVisible(widget, visible);
    }
}

void WindowBase::paintEvent(QPaintEvent* event) {
    Q_D(WindowBase);
    QPainter painter(this);

    if (d->m_backdropEffect == fluent::windowing::BackdropEffect::Solid) {
        painter.fillRect(rect(), themeColorsRef().bgLayer);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
    }
}

} // namespace manga::ui::window
