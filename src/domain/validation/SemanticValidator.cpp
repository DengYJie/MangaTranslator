#include "domain/validation/SemanticValidator.h"

#include <format>
#include <unordered_set>

namespace manga::domain {

ValidationResult SemanticValidator::validate(const ComicProject& project) {
    std::vector<ValidationError> errors;

    if (!project.id().isValid()) {
        errors.push_back({.code = ProjectErrorCode::InvalidId,
                          .message = "Project ID must be a non-nil valid RFC 4122 UUID v4",
                          .path = "id"});
    }

    if (project.name().empty()) {
        errors.push_back(
            {.code = ProjectErrorCode::InvalidSemanticData, .message = "Project name cannot be empty", .path = "name"});
    }

    if (project.schemaVersion() != ComicProject::kCurrentSchemaVersion) {
        errors.push_back({.code = ProjectErrorCode::UnsupportedSchemaVersion,
                          .message = std::format("Unsupported schema version: {}, supported is {}",
                                                 project.schemaVersion(),
                                                 ComicProject::kCurrentSchemaVersion),
                          .path = "schemaVersion"});
    }

    if (project.formatVersion() != ComicProject::kCurrentFormatVersion) {
        errors.push_back({.code = ProjectErrorCode::UnsupportedFormatVersion,
                          .message = std::format("Unsupported format version: {}, supported is {}",
                                                 project.formatVersion(),
                                                 ComicProject::kCurrentFormatVersion),
                          .path = "formatVersion"});
    }

    if (project.sourceLanguage().empty()) {
        errors.push_back({.code = ProjectErrorCode::InvalidSemanticData,
                          .message = "Source language code cannot be empty",
                          .path = "sourceLanguage"});
    }

    if (project.targetLanguage().empty()) {
        errors.push_back({.code = ProjectErrorCode::InvalidSemanticData,
                          .message = "Target language code cannot be empty",
                          .path = "targetLanguage"});
    }

    if (!project.settings().isValid()) {
        errors.push_back({.code = ProjectErrorCode::InvalidSemanticData,
                          .message = "Project settings contain invalid parameters",
                          .path = "settings"});
    }

    std::unordered_set<PageId> seenPageIds;
    std::unordered_set<RegionId> allRegionIds;

    for (size_t pageIdx = 0; pageIdx < project.pages().size(); ++pageIdx) {
        const auto& page = project.pages()[pageIdx];
        const std::string pagePath = std::format("pages[{}]", pageIdx);
        const std::string pageIdStr = page.id().toString();

        if (!page.id().isValid()) {
            errors.push_back({.code = ProjectErrorCode::InvalidId,
                              .message = "Page ID must be a non-nil valid RFC 4122 UUID v4",
                              .path = pagePath + ".id"});
        } else if (!seenPageIds.insert(page.id()).second) {
            errors.push_back({.code = ProjectErrorCode::DuplicateId,
                              .message = std::format("Duplicate Page ID: {}", pageIdStr),
                              .path = pagePath + ".id",
                              .entityId = pageIdStr});
        }

        // Invariant: page orderIndex must be strictly contiguous 0, 1, ..., N-1
        if (page.orderIndex() != static_cast<int>(pageIdx)) {
            errors.push_back(
                {.code = ProjectErrorCode::DuplicateOrderIndex,
                 .message = std::format(
                     "Page orderIndex must be strictly contiguous (expected {}, got {})", pageIdx, page.orderIndex()),
                 .path = pagePath + ".orderIndex",
                 .entityId = pageIdStr});
        }

        if (!page.imageSize().isValid()) {
            errors.push_back({.code = ProjectErrorCode::InvalidSemanticData,
                              .message = std::format("Invalid page image dimensions: {}x{}",
                                                     page.imageSize().width,
                                                     page.imageSize().height),
                              .path = pagePath + ".imageSize",
                              .entityId = pageIdStr});
        }

        if (page.originalAssetPath().empty()) {
            errors.push_back({.code = ProjectErrorCode::MissingAsset,
                              .message = "Original image asset path cannot be empty",
                              .path = pagePath + ".originalAsset",
                              .entityId = pageIdStr});
        }

        for (size_t regionIdx = 0; regionIdx < page.regions().size(); ++regionIdx) {
            const auto& region = page.regions()[regionIdx];
            const std::string regionPath = std::format("{}.regions[{}]", pagePath, regionIdx);
            const std::string regionIdStr = region.id().toString();

            if (!region.id().isValid()) {
                errors.push_back({.code = ProjectErrorCode::InvalidId,
                                  .message = "Region ID must be a non-nil valid RFC 4122 UUID v4",
                                  .path = regionPath + ".id"});
            } else if (!allRegionIds.insert(region.id()).second) {
                errors.push_back({.code = ProjectErrorCode::DuplicateId,
                                  .message = std::format("Duplicate Region ID across project: {}", regionIdStr),
                                  .path = regionPath + ".id",
                                  .entityId = regionIdStr});
            }

            // Invariant: region orderIndex must be strictly contiguous 0, 1, ..., M-1 within page
            if (region.orderIndex() != static_cast<int>(regionIdx)) {
                errors.push_back(
                    {.code = ProjectErrorCode::DuplicateOrderIndex,
                     .message = std::format("Region orderIndex must be strictly contiguous (expected {}, got {})",
                                            regionIdx,
                                            region.orderIndex()),
                     .path = regionPath + ".orderIndex",
                     .entityId = regionIdStr});
            }

            const auto& geo = region.geometry();
            if (!geo.isValid()) {
                errors.push_back(
                    {.code = ProjectErrorCode::InvalidSemanticData,
                     .message = std::format("Invalid Region geometry (x={}, y={}, w={}, h={}), minimum size is 2x2",
                                            geo.x,
                                            geo.y,
                                            geo.width,
                                            geo.height),
                     .path = regionPath + ".geometry",
                     .entityId = regionIdStr});
            } else if (page.imageSize().isValid() && !geo.isWithinBounds(page.imageSize())) {
                errors.push_back({.code = ProjectErrorCode::OutOfBoundsGeometry,
                                  .message = std::format("Region (x={}, y={}, w={}, h={}) exceeds page bounds ({}x{})",
                                                         geo.x,
                                                         geo.y,
                                                         geo.width,
                                                         geo.height,
                                                         page.imageSize().width,
                                                         page.imageSize().height),
                                  .path = regionPath + ".geometry",
                                  .entityId = regionIdStr});
            }

            const auto& layout = region.layout();
            if (!layout.isValid()) {
                errors.push_back(
                    {.code = ProjectErrorCode::InvalidLayout,
                     .message =
                         "Invalid Region text layout parameters (fontSize, fontWeight, color hex, stroke, or margins)",
                     .path = regionPath + ".layout",
                     .entityId = regionIdStr});
            }

            const auto& clear = region.clear();
            if (!clear.isValid()) {
                errors.push_back({.code = ProjectErrorCode::InvalidClearSettings,
                                  .message = std::format(
                                      "Invalid ClearSettings opacity ({}), must be in range [0.0, 1.0]", clear.opacity),
                                  .path = regionPath + ".clear",
                                  .entityId = regionIdStr});
            }

            const auto& trans = region.translation();
            if (trans.selectedCandidateId().has_value() && !trans.selectedCandidateId()->empty()) {
                if (!trans.hasCandidate(*trans.selectedCandidateId())) {
                    errors.push_back(
                        {.code = ProjectErrorCode::InvalidCandidateSelection,
                         .message = std::format("selectedCandidateId '{}' does not exist in candidates list",
                                                *trans.selectedCandidateId()),
                         .path = regionPath + ".translation.selectedCandidateId",
                         .entityId = regionIdStr});
                }
            }
        }
    }

    std::unordered_set<GlossaryId> seenGlossaryIds;
    for (size_t gIdx = 0; gIdx < project.glossary().size(); ++gIdx) {
        const auto& g = project.glossary()[gIdx];
        const std::string gPath = std::format("glossary[{}]", gIdx);
        if (!g.isValid() || !g.id.isValid()) {
            errors.push_back({.code = ProjectErrorCode::InvalidSemanticData,
                              .message = "Glossary entry must have valid UUID v4 ID and non-empty source and target",
                              .path = gPath});
        } else if (!seenGlossaryIds.insert(g.id).second) {
            errors.push_back({.code = ProjectErrorCode::DuplicateId,
                              .message = std::format("Duplicate Glossary ID: {}", g.id.toString()),
                              .path = gPath});
        }
    }

    if (!errors.empty()) {
        return std::unexpected(errors);
    }
    return {};
}

ValidationResult SemanticValidator::validatePage(const ComicPage& page) {
    ComicProject tempProject("Temp");
    tempProject.addPage(page);
    return validate(tempProject);
}

ValidationResult
SemanticValidator::validateRegion(const TextRegion& region, const ImageSize& pageBounds, int pageIndex) {
    ComicProject tempProject("Temp");
    ComicPage page(PageId::generate(), "assets/original/temp.png", pageBounds, pageIndex);
    page.addRegion(region);
    tempProject.addPage(page);
    return validate(tempProject);
}

} // namespace manga::domain
