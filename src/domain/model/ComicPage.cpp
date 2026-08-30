#include "domain/model/ComicPage.h"

#include <algorithm>
#include <unordered_set>

namespace manga::domain {

bool ComicPage::hasRegion(const RegionId& regionId) const noexcept {
    return std::ranges::any_of(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
}

const TextRegion* ComicPage::findRegion(const RegionId& regionId) const noexcept {
    const auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    return (it != m_regions.end()) ? &(*it) : nullptr;
}

bool ComicPage::addRegion(TextRegion region) {
    if (!region.isValid()) {
        return false;
    }
    // Creation boundary invariant: strictly inside page image bounds
    if (m_imageSize.isValid() && !region.geometry().isWithinBounds(m_imageSize)) {
        return false;
    }
    if (hasRegion(region.id())) {
        return false;
    }
    // Automatically maintain contiguous 0..N-1 orderIndex
    region.setOrderIndex(static_cast<int>(m_regions.size()));
    m_regions.push_back(std::move(region));
    return true;
}

std::expected<void, ProjectEditError> ComicPage::insertRegion(TextRegion region, int orderIndex) {
    if (!region.isValid()) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::EditGeometryRejected,
                                                .message = "Region geometry is invalid",
                                                .entityId = region.id().toString()});
    }
    if (m_imageSize.isValid() && !region.geometry().isWithinBounds(m_imageSize)) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::OutOfBoundsGeometry,
                                                .message = "Region geometry is outside the page bounds",
                                                .entityId = region.id().toString()});
    }
    if (hasRegion(region.id())) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::DuplicateId,
                                                .message = "Region ID already exists on this page",
                                                .entityId = region.id().toString()});
    }
    const int currentSize = static_cast<int>(m_regions.size());
    if (orderIndex < 0 || orderIndex > currentSize) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::EditOrderRejected,
                                                .message = "Insert order index is out of range",
                                                .entityId = region.id().toString()});
    }

    // Build the new region list on a temporary vector, then swap once
    std::vector<TextRegion> newRegions;
    newRegions.reserve(static_cast<size_t>(currentSize + 1));
    for (int i = 0; i < orderIndex; ++i) {
        newRegions.push_back(m_regions[static_cast<size_t>(i)]);
    }
    region.setOrderIndex(orderIndex);
    newRegions.push_back(std::move(region));
    for (int i = orderIndex; i < currentSize; ++i) {
        newRegions.push_back(m_regions[static_cast<size_t>(i)]);
    }
    // Re-normalize order indices to ensure contiguity
    for (size_t i = 0; i < newRegions.size(); ++i) {
        newRegions[i].setOrderIndex(static_cast<int>(i));
    }
    m_regions = std::move(newRegions);
    return {};
}

bool ComicPage::loadRegion(TextRegion region) {
    if (!region.isValid()) {
        return false;
    }
    if (m_imageSize.isValid() && !region.geometry().isWithinBounds(m_imageSize)) {
        return false;
    }
    if (hasRegion(region.id())) {
        return false;
    }
    // Caller must supply the correct next index
    if (region.orderIndex() != static_cast<int>(m_regions.size())) {
        return false;
    }
    m_regions.push_back(std::move(region));
    return true;
}

bool ComicPage::removeRegion(const RegionId& regionId) {
    const auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }
    m_regions.erase(it);
    normalizeRegionOrder();
    return true;
}

bool ComicPage::reorderRegion(const RegionId& regionId, int newOrderIndex) {
    if (newOrderIndex < 0 || newOrderIndex >= static_cast<int>(m_regions.size())) {
        return false;
    }
    const auto it =
        std::ranges::find_if(m_regions, [&regionId](const TextRegion& region) { return region.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }

    const auto currentIndex = std::distance(m_regions.begin(), it);
    if (currentIndex == newOrderIndex) {
        return true;
    }

    TextRegion region = std::move(*it);
    m_regions.erase(it);
    m_regions.insert(m_regions.begin() + newOrderIndex, std::move(region));
    normalizeRegionOrder();
    return true;
}

std::optional<RectGeometry> ComicPage::updateRegionGeometry(const RegionId& regionId, const RectGeometry& newGeometry) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return std::nullopt;
    }
    if (!m_imageSize.isValid()) {
        return std::nullopt;
    }
    const auto clamped = newGeometry.clampedToBounds(m_imageSize);
    if (!clamped.has_value()) {
        return std::nullopt;
    }
    it->setGeometry(*clamped);
    return clamped;
}

std::expected<void, ProjectEditError> ComicPage::updateRegionSource(const RegionId& regionId, TextContent source) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Region not found", .entityId = regionId.toString()});
    }
    it->setSource(std::move(source));
    return {};
}

std::expected<void, ProjectEditError> ComicPage::updateRegionTranslation(const RegionId& regionId,
                                                                         TranslationEntry translation) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Region not found", .entityId = regionId.toString()});
    }
    it->setTranslation(std::move(translation));
    return {};
}

std::expected<void, ProjectEditError> ComicPage::updateRegionLayout(const RegionId& regionId, TextLayout layout) {
    if (!layout.isValid()) {
        return std::unexpected(
            ProjectEditError{.code = ProjectErrorCode::EditLayoutRejected,
                             .message = "Layout is invalid (fontSizePt > 0, lineSpacing > 0, valid margins required)",
                             .entityId = regionId.toString()});
    }
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Region not found", .entityId = regionId.toString()});
    }
    it->setLayout(std::move(layout));
    return {};
}

bool ComicPage::setRegionEnabled(const RegionId& regionId, bool enabled) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }
    it->setEnabled(enabled);
    return true;
}

bool ComicPage::setRegionLocked(const RegionId& regionId, bool locked) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }
    it->setLocked(locked);
    return true;
}

bool ComicPage::setRegionTranslationVisible(const RegionId& regionId, bool visible) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }
    it->setTranslationVisible(visible);
    return true;
}

bool ComicPage::setRegionIsCleared(const RegionId& regionId, bool cleared) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) { return r.id() == regionId; });
    if (it == m_regions.end()) {
        return false;
    }
    it->setIsCleared(cleared);
    return true;
}

bool ComicPage::applyClearResult(const RegionId& regionId, std::string relativeLayerPath) {
    if (!hasRegion(regionId)) {
        return false;
    }
    setClearLayerAsset(std::move(relativeLayerPath));
    return setRegionIsCleared(regionId, true);
}

void ComicPage::normalizeRegionOrder() noexcept {
    for (size_t i = 0; i < m_regions.size(); ++i) {
        m_regions[i].setOrderIndex(static_cast<int>(i));
    }
}

bool ComicPage::isValid() const noexcept {
    if (!m_id.isValid() || m_orderIndex < 0 || !m_imageSize.isValid() || m_originalAssetPath.empty()) {
        return false;
    }

    std::unordered_set<RegionId> seenIds;

    for (size_t i = 0; i < m_regions.size(); ++i) {
        const auto& region = m_regions[i];
        if (!region.isValid()) {
            return false;
        }
        if (!region.geometry().isWithinBounds(m_imageSize)) {
            return false;
        }
        if (!seenIds.insert(region.id()).second) {
            return false;
        }
        if (region.orderIndex() != static_cast<int>(i)) {
            return false;
        }
    }

    return true;
}

bool ComicPage::operator==(const ComicPage& other) const noexcept {
    return m_id == other.m_id && m_originalAssetPath == other.m_originalAssetPath && m_imageSize == other.m_imageSize &&
           m_orderIndex == other.m_orderIndex && m_regions == other.m_regions &&
           m_clearLayerAsset == other.m_clearLayerAsset && m_translationLayerAsset == other.m_translationLayerAsset &&
           m_manualOverrideLayerAsset == other.m_manualOverrideLayerAsset;
}

} // namespace manga::domain
