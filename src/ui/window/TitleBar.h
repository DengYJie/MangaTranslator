#pragma once

#include <FluentQt/BasicInput.h>
#include <FluentQt/Windowing.h>
#include <QIcon>
#include <QString>
#include <QWidget>

namespace manga::ui::animation {
class AnimatedIcon;
}

namespace manga::ui::window {

class ElidedLabel;

/**
 * @brief WinUI 3 standard window title bar.
 *
 * Follows the Windows 11 Fluent title bar design guidance: 32px standard height and a
 * 48px tall option (for hosting a search box or avatar), 16px spec safe margins,
 * full-bleed caption backplate buttons, text elision, and seamless integration with
 * native system window events.
 */
class TitleBar : public fluent::windowing::TitleBar {
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)

    Q_PROPERTY(QString subtitle READ subtitle WRITE setSubtitle NOTIFY titleChanged)

    Q_PROPERTY(QIcon icon READ icon WRITE setIcon NOTIFY iconChanged)

    Q_PROPERTY(
        bool backButtonVisible READ isBackButtonVisible WRITE setBackButtonVisible NOTIFY backButtonVisibleChanged)

    Q_PROPERTY(
        bool backButtonEnabled READ isBackButtonEnabled WRITE setBackButtonEnabled NOTIFY backButtonEnabledChanged)

    Q_PROPERTY(bool paneToggleButtonVisible READ isPaneToggleButtonVisible WRITE setPaneToggleButtonVisible NOTIFY
                   paneToggleButtonVisibleChanged)

    Q_PROPERTY(HeightOption heightOption READ heightOption WRITE setHeightOption NOTIFY heightOptionChanged)

    Q_PROPERTY(
        int minDragRegionWidth READ minDragRegionWidth WRITE setMinDragRegionWidth NOTIFY minDragRegionWidthChanged)

public:
    /**
     * @brief Title bar height options.
     */
    enum class HeightOption {
        // Standard height: WinUI 3 default 32px.
        Standard,
        // Tall height: 48px when hosting a search box or user card.
        Tall
    };

    Q_ENUM(HeightOption)

    /**
     * @brief Constructor.
     * @param parent Parent container widget.
     */
    explicit TitleBar(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~TitleBar() override;

    /**
     * @brief Sets the application window main title.
     * @param title Main title text.
     */
    void setTitle(const QString& title);

    /**
     * @brief Returns the application window main title.
     * @return Current main title text.
     */
    QString title() const;

    /**
     * @brief Sets the application window subtitle.
     * @param subtitle Subtitle text.
     */
    void setSubtitle(const QString& subtitle);

    /**
     * @brief Returns the application window subtitle.
     * @return Current subtitle text.
     */
    QString subtitle() const;

    /**
     * @brief Sets the application window icon.
     * @param icon Icon object.
     */
    void setIcon(const QIcon& icon);

    /**
     * @brief Returns the application window icon.
     * @return Current icon object.
     */
    QIcon icon() const;

    /**
     * @brief Sets the visibility of the back navigation button.
     * @param visible Whether it is visible.
     */
    void setBackButtonVisible(bool visible);

    /**
     * @brief Queries the visibility of the back navigation button.
     * @return Whether it is visible.
     */
    bool isBackButtonVisible() const;

    /**
     * @brief Sets the interaction enabled state of the back navigation button.
     * @param enabled Whether it is enabled.
     */
    void setBackButtonEnabled(bool enabled);

    /**
     * @brief Queries the interaction enabled state of the back navigation button.
     * @return Whether it is enabled.
     */
    bool isBackButtonEnabled() const;

    /**
     * @brief Sets the visibility of the pane expand/collapse toggle button.
     * @param visible Whether it is visible.
     */
    void setPaneToggleButtonVisible(bool visible);

    /**
     * @brief Queries the visibility of the pane expand/collapse toggle button.
     * @return Whether it is visible.
     */
    bool isPaneToggleButtonVisible() const;

    /**
     * @brief Sets the title bar height mode.
     * @param option Target height mode.
     */
    void setHeightOption(HeightOption option);

    /**
     * @brief Returns the current title bar height mode.
     * @return Current height mode.
     */
    HeightOption heightOption() const;

    /**
     * @brief Content area alignment.
     */
    enum class ContentAlignment {
        // Absolutely centered alignment.
        Center,
        // Adaptive stretch alignment.
        Stretch
    };

    Q_ENUM(ContentAlignment)

    /**
     * @brief Injects the central main content area.
     * @param contentWidget Custom content widget pointer.
     * @param alignment Alignment mode.
     */
    void setContentWidget(QWidget* contentWidget, ContentAlignment alignment = ContentAlignment::Stretch);

    /**
     * @brief Returns the central main content area widget.
     * @return Content widget pointer.
     */
    QWidget* contentWidget() const;

    /**
     * @brief Returns all internal widgets that must receive mouse click events.
     * WindowBase registers them with QWindowKit for hit-test passthrough so they are
     * not treated as frameless drag regions.
     */
    QList<QWidget*> hitTestVisibleWidgets() const;

    /**
     * @brief Returns the alignment of the current central content area.
     * @return Alignment enum value.
     */
    ContentAlignment contentAlignment() const;

    /**
     * @brief Injects custom right header content.
     * @param rightHeaderWidget Right-side custom widget pointer (for example a user avatar area).
     */
    void setRightHeaderWidget(QWidget* rightHeaderWidget);

    /**
     * @brief Returns the custom right header content widget.
     * @return Right-side custom widget pointer.
     */
    QWidget* rightHeaderWidget() const;

    /**
     * @brief Injects custom left header content.
     * @param leftHeaderWidget Left-side custom widget pointer.
     */
    void setLeftHeaderWidget(QWidget* leftHeaderWidget);

    /**
     * @brief Returns the custom left header content widget.
     * @return Left-side custom widget pointer.
     */
    QWidget* leftHeaderWidget() const;

    /**
     * @brief Sets the minimum drag safe-zone width between the right header and the
     * system caption buttons.
     * @param width Width in pixels; defaults to 48px (WinUI 3 standard); 0 means flush
     * against the caption buttons.
     */
    void setMinDragRegionWidth(int width);

    /**
     * @brief Returns the right-side minimum drag safe-zone width.
     * @return Width in pixels.
     */
    int minDragRegionWidth() const;

    /**
     * @brief System caption button types.
     */
    enum class SystemButtonType {
        Minimize,
        Maximize,
        Close
    };

    Q_ENUM(SystemButtonType)

    /**
     * @brief Returns the corresponding system caption button, used by QWindowKit to
     * register the system minimize/maximize/close actions.
     * @param type Button type.
     * @return Button widget pointer.
     */
    QWidget* systemButton(SystemButtonType type) const;

    /**
     * @brief Sets a system caption button's visibility and refreshes the layout.
     * @param type Button type.
     * @param visible Whether it is visible.
     */
    void setSystemButtonVisible(SystemButtonType type, bool visible);

    /**
     * @brief Queries a system caption button's visibility.
     * @param type Button type.
     * @return Whether it is visible.
     */
    bool isSystemButtonVisible(SystemButtonType type) const;

    /**
     * @brief Theme refresh callback: synchronizes foreground colors, corner radii, and
     * inactive-state opacity.
     */
    void onThemeUpdated() override;

Q_SIGNALS:
    /**
     * @brief Back button clicked signal.
     */

    void backButtonClicked();

    /**
     * @brief Pane toggle button clicked signal.
     */
    void paneToggleButtonClicked();

    /**
     * @brief Main title changed signal.
     * @param title New title content.
     */
    void titleChanged(const QString& title);

    /**
     * @brief Subtitle changed signal.
     * @param subtitle New subtitle content.
     */
    void subtitleChanged(const QString& subtitle);

    /**
     * @brief Icon changed signal.
     * @param icon New icon.
     */
    void iconChanged(const QIcon& icon);

    /**
     * @brief Back button visibility changed signal.
     * @param visible New visibility state.
     */
    void backButtonVisibleChanged(bool visible);

    void backButtonEnabledChanged(bool enabled);

    /**
     * @brief Pane toggle button visibility changed signal.
     * @param visible New visibility state.
     */
    void paneToggleButtonVisibleChanged(bool visible);

    /**
     * @brief Title bar height mode changed signal.
     * @param option New height mode.
     */
    void heightOptionChanged(HeightOption option);

    /**
     * @brief Minimum drag safe-zone width changed signal.
     * @param width New width in pixels.
     */
    void minDragRegionWidthChanged(int width);

    /**
     * @brief Interactive widget changed signal (used by WindowBase / QWindowKit to
     * dynamically synchronize passthrough regions).
     * @param widget The widget that changed.
     * @param visible Whether it needs to receive mouse interaction (true for an
     * interactive widget, false for a window drag region).
     */
    void hitTestWidgetChanged(QWidget* widget, bool visible);

protected:
    // showEvent is overridden to install the event filter once the title bar has
    // been added to a window tree.
    void showEvent(QShowEvent* event) override;

    // Watches size changes to dynamically compute the absolutely centered content
    // area and the elided text region.
    void resizeEvent(QResizeEvent* event) override;

    // Watches the host window state to synchronize the maximize/restore icon and
    // the top-right corner clipping.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Synchronizes the current window's maximized/restored state into the UI.
    void syncWindowState();

private:
    // Computes and applies the physical height from the present child widgets and
    // the height mode.
    void updateHeight();

    // Computes absolute pixel coordinates for each region and updates the drag
    // safe zone.
    void updateLayout();

    // Ensures the caption button backplates fill the title bar height.
    void updateCaptionButtonSizes();

    // Lowers the opacity of interactive elements while the window is inactive.
    void syncActivationOpacity();

    // Builds a system caption button with WinUI 3 interaction states and tooltip.
    fluent::basicinput::Button* createCaptionButton(const QString& objectName,
                                                    const QString& glyph,
                                                    const QString& tooltip,
                                                    bool isCloseBtn = false);

    QString m_title;
    QString m_subtitle;
    QIcon m_icon;

    animation::AnimatedIcon* m_animatedBackIcon = nullptr;
    animation::AnimatedIcon* m_animatedPaneToggleIcon = nullptr;

    bool m_backButtonVisible = false;
    bool m_backButtonEnabled = true;
    bool m_paneToggleButtonVisible = false;
    HeightOption m_heightOption = HeightOption::Standard;
    int m_minDragRegionWidth = 48;

    // Internal state tracking.
    bool m_isCompact = false;
    int m_compactModeThresholdWidth = 0;

    // Custom injected widgets.
    QWidget* m_leftHeaderWidget = nullptr;
    QWidget* m_contentWidget = nullptr;
    ContentAlignment m_contentAlignment = ContentAlignment::Stretch;
    QWidget* m_rightHeaderWidget = nullptr;

    // Title and navigation controls.
    fluent::basicinput::Button* m_backButton = nullptr;
    fluent::basicinput::Button* m_paneToggleButton = nullptr;
    fluent::basicinput::Button* m_iconButton = nullptr;
    ElidedLabel* m_titleLabel = nullptr;
    ElidedLabel* m_subtitleLabel = nullptr;

    // Window system caption buttons.
    fluent::basicinput::Button* m_minimizeButton = nullptr;
    fluent::basicinput::Button* m_maximizeButton = nullptr;
    fluent::basicinput::Button* m_closeButton = nullptr;
};
} // namespace manga::ui::window
