#pragma once

#include <cmath>
#include <string>

namespace manga::domain {

/// Configuration for region background text clearing / inpainting.
struct ClearSettings {
    bool enabled{true};
    std::string colorHex{"#FFFFFF"};
    double opacity{1.0}; // Opacity in range [0.0, 1.0]

    [[nodiscard]] bool isValid() const noexcept {
        return !std::isnan(opacity) && opacity >= 0.0 && opacity <= 1.0;
    }

    [[nodiscard]] bool operator==(const ClearSettings& other) const noexcept {
        return enabled == other.enabled && colorHex == other.colorHex && std::abs(opacity - other.opacity) <= 1e-5;
    }
};

} // namespace manga::domain
