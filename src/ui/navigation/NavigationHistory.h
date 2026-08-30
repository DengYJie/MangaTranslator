#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::navigation {

/// Back/forward stacks for shell navigation. Targets are committed only after the
/// presenter reports a successful render, so the history never diverges from the visible page.
class NavigationHistory {
public:
    /// Pushes a route after a successful present; drops any forward entries.
    void push(const QString& routeKey);

    [[nodiscard]] bool canGoBack() const {
        return m_index > 0;
    }
    [[nodiscard]] bool canGoForward() const {
        return m_index >= 0 && m_index + 1 < m_entries.size();
    }

    /// Peeks at the route goBack/goForward would present. Does not mutate.
    [[nodiscard]] std::optional<QString> backTarget() const;
    [[nodiscard]] std::optional<QString> forwardTarget() const;

    /// Commits the cursor move after the back/forward present succeeded.
    void commitBack();
    void commitForward();

    [[nodiscard]] std::optional<QString> current() const;
    void clear();

private:
    QVector<QString> m_entries;
    qsizetype m_index{-1};
};

} // namespace manga::ui::navigation
