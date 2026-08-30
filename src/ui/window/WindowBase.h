#pragma once

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Windowing.h>
#include <QWidget>

namespace manga::ui::window {

class TitleBar;
class WindowBasePrivate;

/**
 * @brief Custom window base class implementing modern frameless chrome and native
 * system effects on top of QWidget + QWindowKit.
 * @note Uses the Pimpl idiom to hide the QWindowKit implementation and keep the ABI stable.
 */
class WindowBase : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
    Q_PROPERTY(QWidget* contentWidget READ contentWidget WRITE setContentWidget NOTIFY contentWidgetChanged)

    Q_PROPERTY(fluent::windowing::BackdropEffect backdropEffect READ backdropEffect WRITE setBackdropEffect NOTIFY
                   backdropEffectChanged)

    Q_PROPERTY(bool customWindowChromeEnabled READ customWindowChromeEnabled WRITE setCustomWindowChromeEnabled NOTIFY
                   customWindowChromeEnabledChanged)

public:
    /**
     * @brief Constructor. Initializes the base layout.
     */
    explicit WindowBase(QWidget* parent = nullptr);

    ~WindowBase() override;

    /**
     * @brief Returns the currently hosted main content widget.
     */
    QWidget* contentWidget() const;

    /**
     * @brief Sets the main content widget. Usually called while building the window UI.
     * @param widget The main content widget to embed; its lifetime is managed by WindowBase.
     */
    void setContentWidget(QWidget* widget);

    /**
     * @brief Returns the title bar widget, used to add custom title bar actions or components.
     */
    TitleBar* titleBar() const;

    /**
     * @brief Returns the content area container widget.
     */
    QWidget* contentHost() const;

    /**
     * @brief Sets the window backdrop effect (Mica / Acrylic / Solid).
     * @note Recommended to call during window initialization or on theme changes.
     * @param effect The target backdrop effect type.
     */
    void setBackdropEffect(fluent::windowing::BackdropEffect effect);

    /**
     * @brief Returns the currently active backdrop effect.
     */
    fluent::windowing::BackdropEffect backdropEffect() const;

    /**
     * @brief Enables or disables the Fluent owner-drawn title bar.
     * @note Set this before calling show() on the window. When false, the window keeps
     * the native system frame.
     * @param enabled Whether the owner-drawn immersive title bar is enabled.
     */
    void setCustomWindowChromeEnabled(bool enabled);

    /**
     * @brief Returns whether the owner-drawn title bar is enabled.
     */
    bool customWindowChromeEnabled() const;

    /**
     * @brief Sets the hit-test visibility of a child widget inside the owner-drawn title bar area.
     * @param widget Target widget pointer.
     * @param visible Whether the widget responds to mouse interaction (when false the area
     * acts as a title bar drag region).
     */
    void setHitTestVisible(QWidget* widget, bool visible = true);

    /**
     * @brief Theme update callback. Triggered automatically when the system or application
     * switches between light and dark mode.
     */
    void onThemeUpdated() override;

Q_SIGNALS:

    void contentWidgetChanged();

    void backdropEffectChanged();

    void customWindowChromeEnabledChanged();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Q_DECLARE_PRIVATE(WindowBase)
    const std

        ::unique_ptr<WindowBasePrivate>
            d_ptr;
};

} // namespace manga::ui::window
