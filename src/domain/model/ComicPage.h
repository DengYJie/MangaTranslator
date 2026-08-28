#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/TextRegion.h"
#include "domain/model/Uuid.h"
#include "domain/validation/ValidationResult.h"

#include <optional>
#include <string>
#include <vector>

namespace manga::domain {

/// A single comic page containing original image reference, dimensions, order, and text regions.
class ComicPage {
public:
    ComicPage() : m_id(PageId::generate()) {}
    explicit ComicPage(PageId id) : m_id(id) {}
    ComicPage(PageId id, std::string originalAssetPath, ImageSize size, int orderIndex = 0)
        : m_id(id), m_originalAssetPath(std::move(originalAssetPath)), m_imageSize(size), m_orderIndex(orderIndex) {}

    [[nodiscard]] const PageId& id() const noexcept {
        return m_id;
    }
    [[nodiscard]] const std::string& originalAssetPath() const noexcept {
        return m_originalAssetPath;
    }
    [[nodiscard]] const ImageSize& imageSize() const noexcept {
        return m_imageSize;
    }
    [[nodiscard]] int orderIndex() const noexcept {
        return m_orderIndex;
    }

    [[nodiscard]] const std::vector<TextRegion>& regions() const noexcept {
        return m_regions;
    }

    // Layer asset paths (non-destructive derived / manual layers)
    [[nodiscard]] const std::optional<std::string>& clearLayerAsset() const noexcept {
        return m_clearLayerAsset;
    }
    [[nodiscard]] const std::optional<std::string>& translationLayerAsset() const noexcept {
        return m_translationLayerAsset;
    }
    [[nodiscard]] const std::optional<std::string>& manualOverrideLayerAsset() const noexcept {
        return m_manualOverrideLayerAsset;
    }

    // Page-level mutators
    void setOriginalAssetPath(std::string path) {
        m_originalAssetPath = std::move(path);
    }
    void setImageSize(ImageSize size) noexcept {
        m_imageSize = size;
    }
    void setOrderIndex(int index) noexcept {
        m_orderIndex = index;
    }

    void setClearLayerAsset(std::optional<std::string> path) {
        m_clearLayerAsset = std::move(path);
    }
    void setTranslationLayerAsset(std::optional<std::string> path) {
        m_translationLayerAsset = std::move(path);
    }
    void setManualOverrideLayerAsset(std::optional<std::string> path) {
        m_manualOverrideLayerAsset = std::move(path);
    }

    // Region Lifecycle (for ComicProject's use; validates page-internal invariants)

    [[nodiscard]] bool hasRegion(const RegionId& regionId) const noexcept;
    [[nodiscard]] const TextRegion* findRegion(const RegionId& regionId) const noexcept;

    /// Runtime: adds a new region to the page.
    /// Validates: unique ID, isValid(), strictly within image bounds.
    /// Automatically assigns contiguous orderIndex = regions.size().
    bool addRegion(TextRegion region);

    /// Removes a region by ID and re-normalizes orderIndex to contiguous 0..N-1.
    bool removeRegion(const RegionId& regionId);

    [[nodiscard]] bool reorderRegion(const RegionId& regionId, int newOrderIndex);

    // Region Content Mutations (maintain revision, validate constraints)

    /// Updates region geometry with safe boundary clamping to image bounds.
    /// Returns clamped geometry on success, or std::nullopt if clamped size < 2×2.
    std::optional<RectGeometry> updateRegionGeometry(const RegionId& regionId, const RectGeometry& newGeometry);

    /// Updates region OCR/manual source text and bumps contentRevision.
    [[nodiscard]] std::expected<void, ProjectEditError> updateRegionSource(const RegionId& regionId,
                                                                           TextContent source);

    /// Updates region translation entry and bumps contentRevision.
    [[nodiscard]] std::expected<void, ProjectEditError> updateRegionTranslation(const RegionId& regionId,
                                                                                TranslationEntry translation);

    /// Updates region text layout (validates layout invariants) and bumps contentRevision.
    [[nodiscard]] std::expected<void, ProjectEditError> updateRegionLayout(const RegionId& regionId, TextLayout layout);

    /// Sets region enabled/locked/translationVisible/isCleared flags (no revision bump needed).
    bool setRegionEnabled(const RegionId& regionId, bool enabled) noexcept;
    bool setRegionLocked(const RegionId& regionId, bool locked) noexcept;
    bool setRegionTranslationVisible(const RegionId& regionId, bool visible) noexcept;
    bool setRegionIsCleared(const RegionId& regionId, bool cleared) noexcept;

    /// Normalizes region orderIndex values to contiguous integers: 0, 1, 2, ..., N-1.
    void normalizeRegionOrder() noexcept;

    // Page-level Validation

    /// Validates whether a replacement image dimension matches current page dimensions.
    [[nodiscard]] bool canReplaceOriginalImage(const ImageSize& newSize) const noexcept {
        return m_imageSize == newSize;
    }

    /// Validates page structure and invariants (unique IDs, strictly contiguous orderIndex, bounds).
    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] bool operator==(const ComicPage& other) const noexcept;

private:
    friend class PageRehydrationBuilder;

    /// Rehydration (deserialization only): inserts a region without re-assigning orderIndex.
    bool loadRegion(TextRegion region);

    PageId m_id;
    std::string m_originalAssetPath;
    ImageSize m_imageSize;
    int m_orderIndex{0};

    std::vector<TextRegion> m_regions;

    std::optional<std::string> m_clearLayerAsset;
    std::optional<std::string> m_translationLayerAsset;
    std::optional<std::string> m_manualOverrideLayerAsset;
};

} // namespace manga::domain
