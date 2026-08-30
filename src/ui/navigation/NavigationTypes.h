#pragma once

#include <QString>

namespace manga::ui::navigation {

/// Direction of a presented route, used for transition selection and history bookkeeping.
enum class NavigationDirection {
    None,
    Back,
    Forward,
};

} // namespace manga::ui::navigation
