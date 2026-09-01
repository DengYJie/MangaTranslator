#pragma once

#include <FluentQt/Foundation.h>
#include <QSplitter>

namespace manga::ui::widget {

/// Pane splitter whose handle paints a one-pixel hairline instead of a wide
/// strip. The invisible hit area stays draggable while panes keep a flush
/// split look.
class PaneSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit PaneSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

    void refreshHandlePainting();

protected:
    QSplitterHandle* createHandle() override;
};

} // namespace manga::ui::widget
