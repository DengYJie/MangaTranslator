#pragma once

#include <FluentQt/Foundation.h>
#include <QLabel>
#include <QWidget>

namespace manga::ui::screen::import_ {

class FileDropZone : public QWidget, public fluent::FluentElement {
    Q_OBJECT
public:
    explicit FileDropZone(QWidget* parent = nullptr);
    ~FileDropZone() override;

Q_SIGNALS:
    void pathsDropped(const QVector<QString>& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class VisualState {
        Rest,
        DragEnter,
        Rejected
    };

    VisualState m_visualState = VisualState::Rest;
    QLabel* m_iconLabel;
    QLabel* m_textLabel;

    void updateVisualState(VisualState state);
};

} // namespace manga::ui::screen::import_
