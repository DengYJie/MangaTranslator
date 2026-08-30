#include "ui/navigation/NavigationHistory.h"

namespace manga::ui::navigation {

void NavigationHistory::push(const QString& routeKey) {
    if (current() == routeKey) {
        return;
    }
    m_entries.resize(m_index + 1); // drop forward entries
    m_entries.append(routeKey);
    m_index = m_entries.size() - 1;
}

std::optional<QString> NavigationHistory::backTarget() const {
    if (!canGoBack()) {
        return std::nullopt;
    }
    return m_entries.at(m_index - 1);
}

std::optional<QString> NavigationHistory::forwardTarget() const {
    if (!canGoForward()) {
        return std::nullopt;
    }
    return m_entries.at(m_index + 1);
}

void NavigationHistory::commitBack() {
    if (canGoBack()) {
        --m_index;
    }
}

void NavigationHistory::commitForward() {
    if (canGoForward()) {
        ++m_index;
    }
}

std::optional<QString> NavigationHistory::current() const {
    if (m_index < 0 || m_index >= m_entries.size()) {
        return std::nullopt;
    }
    return m_entries.at(m_index);
}

void NavigationHistory::clear() {
    m_entries.clear();
    m_index = -1;
}

} // namespace manga::ui::navigation
