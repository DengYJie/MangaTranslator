#pragma once

namespace manga::ui::navigation {
/**
 * @brief Navigation focus host interface.
 *
 * Abstracts a unified keyboard focus navigation contract so child widgets can
 * delegate arrow-key events upward, closing the focus loop across levels and regions.
 */
class INavigationFocusHost {
public:
    virtual ~INavigationFocusHost() = default;

    /**
     * @brief Moves keyboard focus along the visible navigation items.
     * @param delta Movement step; -1 moves to the previous item, +1 to the next.
     */
    virtual void moveFocusBy(int delta) = 0;
};
} // namespace manga::ui::navigation
