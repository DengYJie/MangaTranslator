#include "domain/model/ComicPage.h"

#include <algorithm>
#include <unordered_set>

namespace manga::domain {

bool ComicPage::hasRegion(const RegionId& regionId) const noexcept {
    return std::ranges::any_of(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
}

const TextRegion* ComicPage::findRegion(const RegionId& regionId) const noexcept {
    const auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
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
    const auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return false;
    }
    m_regions.erase(it);
    normalizeRegionOrder();
    return true;
}

std::optional<RectGeometry> ComicPage::updateRegionGeometry(const RegionId& regionId,
                                                             const RectGeometry& newGeometry) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
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

std::expected<void, ProjectEditError> ComicPage::updateRegionSource(const RegionId& regionId,
                                                                     TextContent source) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound,
            .message = "Region not found",
            .entityId = regionId.toString()
        });
    }
    it->setSource(std::move(source));
    return {};
}

std::expected<void, ProjectEditError> ComicPage::updateRegionTranslation(
    const RegionId& regionId, TranslationEntry translation) {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound,
            .message = "Region not found",
            .entityId = regionId.toString()
        });
    }
    it->setTranslation(std::move(translation));
    return {};
}

std::expected<void, ProjectEditError> ComicPage::updateRegionLayout(const RegionId& regionId,
                                                                     TextLayout layout) {
    if (!layout.isValid()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditLayoutRejected,
            .message = "Layout is invalid (fontSizePt > 0, lineSpacing > 0, valid margins required)",
            .entityId = regionId.toString()
        });
    }
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound,
            .message = "Region not found",
            .entityId = regionId.toString()
        });
    }
    it->setLayout(std::move(layout));
    return {};
}

bool ComicPage::setRegionEnabled(const RegionId& regionId, bool enabled) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return false;
    }
    it->setEnabled(enabled);
    return true;
}

bool ComicPage::setRegionLocked(const RegionId& regionId, bool locked) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return false;
    }
    it->setLocked(locked);
    return true;
}

bool ComicPage::setRegionTranslationVisible(const RegionId& regionId, bool visible) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return false;
    }
    it->setTranslationVisible(visible);
    return true;
}

bool ComicPage::setRegionIsCleared(const RegionId& regionId, bool cleared) noexcept {
    auto it = std::ranges::find_if(m_regions, [&regionId](const TextRegion& r) {
        return r.id() == regionId;
    });
    if (it == m_regions.end()) {
        return false;
    }
    it->setIsCleared(cleared);
    return true;
}

void ComicPage::normalizeRegionOrder() noexcept {
    for (size_t i = 0; i < m_regions.size(); ++i) {
        m_regions[i].setOrderIndex(static_cast<int>(i));
    }
}

bool ComicPage::isValid() const noexcept {
    if (!m_id.isValid() || m_orderIndex < 0 || !m_imageSize.isValid() ||
        m_originalAssetPath.empty()) {
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
    return m_id == other.m_id && m_originalAssetPath == other.m_originalAssetPath &&
           m_imageSize == other.m_imageSize && m_orderIndex == other.m_orderIndex &&
           m_regions == other.m_regions && m_clearLayerAsset == other.m_clearLayerAsset &&
           m_translationLayerAsset == other.m_translationLayerAsset &&
           m_manualOverrideLayerAsset == other.m_manualOverrideLayerAsset;
}

} // namespace manga::domain
