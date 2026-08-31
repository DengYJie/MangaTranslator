#pragma once

#include <FluentQt/Foundation.h>
#include <QPointer>
#include <QWidget>

class QBoxLayout;

namespace fluent::textfields {
class Label;
}

namespace manga::ui::widget {

/// Layout container for a single setting item with title, description, error text,
/// dirty state indicator, and an editor control.
/// Supports responsive horizontal (>=680px) and vertical (<680px) modes without
/// rebuilding widgets or dropping focus.
class SettingsRow : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit SettingsRow(QWidget* parent = nullptr);
    explicit SettingsRow(const QString& title, const QString& description = QString(), QWidget* parent = nullptr);
    ~SettingsRow() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    void setError(const QString& error);
    void clearError();
    [[nodiscard]] QString error() const;
    [[nodiscard]] bool hasError() const;

    void setDirty(bool dirty);
    [[nodiscard]] bool isDirty() const;

    void setEditorWidget(QWidget* widget, bool fillWidthInVertical = true);
    [[nodiscard]] QWidget* editorWidget() const;

    void updateResponsiveLayout(int availableWidth);

    void onThemeUpdated() override;

    [[nodiscard]] bool isHorizontalMode() const {
        return m_isHorizontal;
    }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    void applyMode(bool horizontal);

    QString m_title;
    QString m_description;
    QString m_error;
    bool m_dirty{false};
    bool m_isHorizontal{true};
    bool m_fillWidthInVertical{true};

    QBoxLayout* m_mainLayout{nullptr};
    QWidget* m_textContainer{nullptr};
    QBoxLayout* m_textLayout{nullptr};
    QWidget* m_titleRow{nullptr};
    QBoxLayout* m_titleRowLayout{nullptr};
    fluent::textfields::Label* m_titleLabel{nullptr};
    fluent::textfields::Label* m_dirtyIndicator{nullptr};
    fluent::textfields::Label* m_descLabel{nullptr};
    fluent::textfields::Label* m_errorLabel{nullptr};

    QWidget* m_editorContainer{nullptr};
    QBoxLayout* m_editorLayout{nullptr};
    QPointer<QWidget> m_editorWidget;
};

} // namespace manga::ui::widget
