#pragma once

#include <FluentQt/Foundation.h>
#include <QPointer>
#include <QWidget>

class QResizeEvent;
class QShowEvent;

namespace manga::ui::base {

/// Base class for full-screen pages: theme refresh, default focus and a responsive hook.
class BasePage : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit BasePage(QWidget* parent = nullptr);
    ~BasePage() override = default;

    void onThemeUpdated() override;

    /// Widget focused automatically when the page is shown.
    void setDefaultFocusWidget(QWidget* widget);
    [[nodiscard]] QWidget* defaultFocusWidget() const {
        return m_defaultFocusWidget.data();
    }

protected:
    /// Called from resizeEvent with the page's current width in pixels.
    virtual void updateResponsiveLayout(int availableWidth);

    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QPointer<QWidget> m_defaultFocusWidget;
};

} // namespace manga::ui::base
