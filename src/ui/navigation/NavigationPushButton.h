#pragma once

#include "ui/navigation/NavigationWidget.h"

#include <QString>

#include <memory>

namespace manga::ui::animation {
class AnimatedVisualSource;
class AnimatedIcon;
} // namespace manga::ui::animation

namespace manga::ui::navigation {
/**
 * @brief Basic icon-and-text navigation button.
 *
 * Implements the standard Fluent icon+text rendering, hover background, and the
 * indent interpolation during the collapse transition.
 */
class NavigationPushButton : public NavigationWidget {
    Q_OBJECT

public:
    explicit NavigationPushButton(const QString& iconGlyph,
                                  const QString& text,
                                  bool isSelectable = true,
                                  QWidget* parent = nullptr);

    ~NavigationPushButton() override = default;

    QString text() const {
        return m_text;
    }

    void setText(const QString& text);

    QString iconGlyph() const {
        return m_iconGlyph;
    }

    void setIconGlyph(const QString& glyph);

    /**
     * @brief Returns the currently attached animated vector visual source.
     * @return Vector animation source pointer; nullptr when not set.
     */
    std::shared_ptr<animation::AnimatedVisualSource> visualSource() const {
        return m_visualSource;
    }

    /**
     * @brief Sets the animated vector visual source.
     * @param source Vector animation source smart pointer.
     */
    void setVisualSource(std::shared_ptr<animation::AnimatedVisualSource> source);

    int iconSize() const {
        return m_iconSize;
    }

    void setIconSize(int size);

    QSize sizeHint() const override;

    /**
     * @brief Updates the geometry alignment of the vector animation icon component.
     */
    void updateIconGeometry();

protected:
    void paintEvent(QPaintEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;

    void changeEvent(QEvent* event) override;

    /// Removes the child indent as collapse progresses to fit the 48px compact bar.
    virtual int iconDrawX() const;

    /// Lets subclasses reserve text layout space for right-side elements (such as
    /// an expand chevron or an unread dot).
    virtual int textRightOffset() const {
        return 0;
    }

protected:
    QString m_iconGlyph;
    QString m_text;
    int m_iconSize = Typography::IconSize::Standard;
    std::shared_ptr<animation::AnimatedVisualSource> m_visualSource;
    animation::AnimatedIcon* m_animatedIcon = nullptr;
};
} // namespace manga::ui::navigation
