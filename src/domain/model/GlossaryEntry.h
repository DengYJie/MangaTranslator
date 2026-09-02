#pragma once

#include "domain/model/Uuid.h"

#include <string>

namespace manga::domain {

/// A term translation glossary item stored with the project.
struct GlossaryEntry {
    GlossaryId id{GlossaryId::generate()};
    std::string source;
    std::string target;
    std::string category;
    bool caseSensitive{false};
    bool enabled{true};

    /// Requires a non-nil identifier and a non-empty source and target term.
    [[nodiscard]] bool isValid() const noexcept {
        return !id.value.isNil() && !source.empty() && !target.empty();
    }

    [[nodiscard]] bool operator==(const GlossaryEntry& other) const noexcept = default;
};

} // namespace manga::domain
