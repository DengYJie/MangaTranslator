#pragma once

#include "ui/navigation/NavigationWidget.h"

#include <QString>

class QMouseEvent;

namespace manga::ui::navigation {

/// Navigation section header item.
///
/// Used for visual grouping, left-aligned with the icon start column, and collapses
/// its height and fades out in Compact mode.
class NavigationSectionHeader : public NavigationWidget {
    Q_OBJECT

public:
    explicit NavigationSectionHeader(const QString& text, QWidget* parent = nullptr);
    ~NavigationSectionHeader() override = default;

    QString text() const {
        return m_text;
    }

    void setCompacted(bool compacted) override;
    void setOrientation(Qt::Orientation orientation) override;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateCompactHeight();

private:
    QString m_text;
};

} // namespace manga::ui::navigation
