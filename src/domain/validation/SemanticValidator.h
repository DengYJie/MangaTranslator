#pragma once

#include "domain/model/ComicPage.h"
#include "domain/model/ComicProject.h"
#include "domain/model/TextRegion.h"
#include "domain/validation/ValidationResult.h"

#include <vector>

namespace manga::domain {

/// Checks the whole project aggregate and collects every violation it finds instead of stopping at the
/// first one.
class SemanticValidator {
public:
    /// Validates the full ComicProject aggregate and returns any validation errors.
    [[nodiscard]] static ValidationResult validate(const ComicProject& project);

    /// Validates a single ComicPage and its regions.
    [[nodiscard]] static ValidationResult validatePage(const ComicPage& page);

    /// Validates a single TextRegion against page bounds.
    [[nodiscard]] static ValidationResult
    validateRegion(const TextRegion& region, const ImageSize& pageBounds, int pageIndex = 0);
};

} // namespace manga::domain
