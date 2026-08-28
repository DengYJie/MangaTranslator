#pragma once

namespace manga::domain {

/// Reading order direction for comic pages and panels.
enum class ReadingDirection {
    RightToLeft, // Traditional Japanese manga (RTL)
    LeftToRight  // Western comic / Korean Manhwa / Chinese Manhua (LTR)
};

} // namespace manga::domain
