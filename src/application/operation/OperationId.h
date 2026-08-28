#pragma once

#include "domain/model/Uuid.h"

namespace manga::application {

struct OperationId {
    domain::Uuid value;

    [[nodiscard]] static OperationId generate() {
        return OperationId{domain::Uuid::generateV4()};
    }
    [[nodiscard]] bool isValid() const noexcept {
        return value.isValidV4();
    }
    [[nodiscard]] std::string toString() const {
        return value.toString();
    }
    [[nodiscard]] constexpr auto operator<=>(const OperationId&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const OperationId&) const noexcept = default;
};

} // namespace manga::application
