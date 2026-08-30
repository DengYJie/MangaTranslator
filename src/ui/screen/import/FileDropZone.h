#pragma once

#include <FluentQt/Foundation.h>
#include <QWidget>

namespace fluent::basicinput {
class Button;
}
namespace fluent::textfields {
class Label;
}

namespace manga::ui::screen::import_ {

class FileDropZone : public QWidget, public fluent::FluentElement {
    Q_OBJECT
public:
    explicit FileDropZone(QWidget* parent = nullptr);
    ~FileDropZone() override;

    void onThemeUpdated() override;

Q_SIGNALS:
    void pathsDropped(const QVector<QString>& paths);
    void browseFilesRequested();
    void browseFolderRequested();

protected:
    void changeEvent(QEvent* event) override;
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
    fluent::textfields::Label* m_iconLabel;
    fluent::textfields::Label* m_textLabel;
    fluent::textfields::Label* m_formatLabel;
    fluent::basicinput::Button* m_browseFilesButton;
    fluent::basicinput::Button* m_browseFolderButton;

    void updateVisualState(VisualState state);
    void refreshText();
};

} // namespace manga::ui::screen::import_
