#include "domain/model/ComicProject.h"

#include <algorithm>
#include <unordered_set>

namespace manga::domain {

ComicProject::ComicProject() : m_id(ProjectId::generate()) {}

ComicProject::ComicProject(std::string name, ProjectId id) : m_id(id), m_name(std::move(name)) {}

bool ComicProject::hasPage(const PageId& pageId) const noexcept {
    return std::ranges::any_of(m_pages, [&pageId](const ComicPage& p) { return p.id() == pageId; });
}

const ComicPage* ComicProject::findPage(const PageId& pageId) const noexcept {
    const auto it = std::ranges::find_if(m_pages, [&pageId](const ComicPage& p) { return p.id() == pageId; });
    return (it != m_pages.end()) ? &(*it) : nullptr;
}

ComicPage* ComicProject::findPageMutable(const PageId& pageId) noexcept {
    const auto it = std::ranges::find_if(m_pages, [&pageId](const ComicPage& p) { return p.id() == pageId; });
    return (it != m_pages.end()) ? &(*it) : nullptr;
}

bool ComicProject::hasRegionInAnyPage(const RegionId& regionId) const noexcept {
    return std::ranges::any_of(m_pages, [&regionId](const ComicPage& p) { return p.hasRegion(regionId); });
}

bool ComicProject::addPage(ComicPage page) {
    if (!page.isValid()) {
        return false;
    }
    if (hasPage(page.id())) {
        return false;
    }
    // Maintain contiguous 0..N-1 orderIndex
    page.setOrderIndex(static_cast<int>(m_pages.size()));
    m_pages.push_back(std::move(page));
    markDirty();
    return true;
}

bool ComicProject::loadPage(ComicPage page) {
    if (!page.isValid()) {
        return false;
    }
    if (hasPage(page.id())) {
        return false;
    }
    // Caller must supply the correct next index
    if (page.orderIndex() != static_cast<int>(m_pages.size())) {
        return false;
    }
    m_pages.push_back(std::move(page));
    // Rehydration is not an edit, so unlike addPage this leaves the dirty flag and the revision untouched.
    return true;
}

bool ComicProject::removePage(const PageId& pageId) {
    const auto it = std::ranges::find_if(m_pages, [&pageId](const ComicPage& p) { return p.id() == pageId; });
    if (it == m_pages.end()) {
        return false;
    }
    m_pages.erase(it);
    normalizePageOrder();
    markDirty();
    return true;
}

void ComicProject::normalizePageOrder() noexcept {
    for (size_t i = 0; i < m_pages.size(); ++i) {
        m_pages[i].setOrderIndex(static_cast<int>(i));
    }
}

std::expected<void, ProjectEditError> ComicProject::addRegionToPage(const PageId& pageId, TextRegion region) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    // Project-level uniqueness check
    if (hasRegionInAnyPage(region.id())) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::DuplicateId,
                                                .message = "RegionId already exists in another page",
                                                .entityId = region.id().toString()});
    }
    const RegionId regionId = region.id();
    if (!page->addRegion(std::move(region))) {
        return std::unexpected(
            ProjectEditError{.code = ProjectErrorCode::EditGeometryRejected,
                             .message = "Region rejected (invalid geometry, out-of-bounds, or duplicate page-level ID)",
                             .entityId = regionId.toString()});
    }
    markDirty();
    return {};
}

std::expected<void, ProjectEditError>
ComicProject::insertRegionOnPage(const PageId& pageId, TextRegion region, int orderIndex) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    if (hasRegionInAnyPage(region.id())) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::DuplicateId,
                                                .message = "RegionId already exists in the project",
                                                .entityId = region.id().toString()});
    }
    auto result = page->insertRegion(std::move(region), orderIndex);
    if (!result) {
        return result;
    }
    markDirty();
    return {};
}

std::expected<void, ProjectEditError> ComicProject::removeRegionFromPage(const PageId& pageId,
                                                                         const RegionId& regionId) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    if (!page->removeRegion(regionId)) {
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::EditNotFound,
                                                .message = "Region not found in page",
                                                .entityId = regionId.toString()});
    }
    markDirty();
    return {};
}

std::expected<RectGeometry, ProjectEditError> ComicProject::updateRegionGeometryOnPage(
    const PageId& pageId, const RegionId& regionId, const RectGeometry& newGeometry) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    const auto result = page->updateRegionGeometry(regionId, newGeometry);
    if (!result.has_value()) {
        return std::unexpected(
            ProjectEditError{.code = ProjectErrorCode::EditGeometryRejected,
                             .message = "Geometry update rejected (out-of-bounds after clamping or region not found)",
                             .entityId = regionId.toString()});
    }
    markDirty();
    return *result;
}

std::expected<void, ProjectEditError>
ComicProject::setRegionTranslationOnPage(const PageId& pageId, const RegionId& regionId, TranslationEntry translation) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    auto res = page->updateRegionTranslation(regionId, std::move(translation));
    if (!res) {
        return res;
    }
    markDirty();
    return {};
}

std::expected<void, ProjectEditError>
ComicProject::reorderRegionOnPage(const PageId& pageId, const RegionId& regionId, int newOrderIndex) {
    ComicPage* page = findPageMutable(pageId);
    if (page == nullptr) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound,
            .message = "Page not found",
            .entityId = pageId.toString(),
        });
    }
    if (page->findRegion(regionId) == nullptr) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound,
            .message = "Region not found",
            .entityId = regionId.toString(),
        });
    }
    if (newOrderIndex < 0 || newOrderIndex >= static_cast<int>(page->regions().size())) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditOrderRejected,
            .message = "Region order index is invalid",
            .entityId = regionId.toString(),
        });
    }
    if (!page->reorderRegion(regionId, newOrderIndex)) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditOrderRejected,
            .message = "Region order could not be changed",
            .entityId = regionId.toString(),
        });
    }
    markDirty();
    return {};
}

std::expected<void, ProjectEditError> ComicProject::applyPageRegionEdits(const PageId& pageId,
                                                                         std::span<const PageRegionEdit> edits) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }

    // Save snapshot for rollback
    const ComicPage snapshot = *page;
    bool changed = false;

    for (const auto& edit : edits) {
        if (edit.geometry.has_value()) {
            const auto res = page->updateRegionGeometry(edit.regionId, *edit.geometry);
            if (!res.has_value()) {
                *page = snapshot;
                return std::unexpected(ProjectEditError{.code = ProjectErrorCode::EditGeometryRejected,
                                                        .message = "Geometry update rejected during batch edit",
                                                        .entityId = edit.regionId.toString()});
            }
            changed = true;
        }
        if (edit.source.has_value()) {
            auto res = page->updateRegionSource(edit.regionId, *edit.source);
            if (!res) {
                *page = snapshot;
                return res;
            }
            changed = true;
        }
        if (edit.translation.has_value()) {
            auto res = page->updateRegionTranslation(edit.regionId, *edit.translation);
            if (!res) {
                *page = snapshot;
                return res;
            }
            changed = true;
        }
        if (edit.layout.has_value()) {
            auto res = page->updateRegionLayout(edit.regionId, *edit.layout);
            if (!res) {
                *page = snapshot;
                return res;
            }
            changed = true;
        }
    }

    // Post-edit validation
    if (!page->isValid()) {
        *page = snapshot;
        return std::unexpected(ProjectEditError{.code = ProjectErrorCode::InvalidSemanticData,
                                                .message = "Page failed invariant check after batch region edits"});
    }

    if (changed) {
        markDirty();
    }
    return {};
}

std::expected<void, ProjectEditError> ComicProject::applyClearChangeOnPage(const PageId& pageId,
                                                                           const RegionId& regionId,
                                                                           std::optional<std::string> layerPath,
                                                                           bool isCleared) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    if (!page->hasRegion(regionId)) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Region not found", .entityId = regionId.toString()});
    }

    page->setClearLayerAsset(std::move(layerPath));
    page->setRegionIsCleared(regionId, isCleared);
    markDirty();
    return {};
}

std::expected<void, ProjectEditError> ComicProject::setClearLayerAssetOnPage(const PageId& pageId,
                                                                             std::optional<std::string> layerPath) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    page->setClearLayerAsset(std::move(layerPath));
    markDirty();
    return {};
}

std::expected<void, ProjectEditError>
ComicProject::setManualOverrideLayerAssetOnPage(const PageId& pageId, std::optional<std::string> layerPath) {
    ComicPage* page = findPageMutable(pageId);
    if (!page) {
        return std::unexpected(ProjectEditError{
            .code = ProjectErrorCode::EditNotFound, .message = "Page not found", .entityId = pageId.toString()});
    }
    page->setManualOverrideLayerAsset(std::move(layerPath));
    markDirty();
    return {};
}

bool ComicProject::addGlossaryEntry(GlossaryEntry entry) {
    if (!entry.isValid()) {
        return false;
    }
    const auto it = std::ranges::find_if(m_glossary, [&entry](const GlossaryEntry& g) { return g.id == entry.id; });
    if (it != m_glossary.end()) {
        return false;
    }
    m_glossary.push_back(std::move(entry));
    markDirty();
    return true;
}

bool ComicProject::removeGlossaryEntry(const GlossaryId& id) {
    const auto it = std::ranges::find_if(m_glossary, [&id](const GlossaryEntry& g) { return g.id == id; });
    if (it == m_glossary.end()) {
        return false;
    }
    m_glossary.erase(it);
    markDirty();
    return true;
}

bool ComicProject::isValid() const noexcept {
    if (!m_id.isValid() || m_name.empty() || m_schemaVersion <= 0 || m_formatVersion <= 0) {
        return false;
    }
    if (m_sourceLanguage.empty() || m_targetLanguage.empty()) {
        return false;
    }
    if (!m_settings.isValid()) {
        return false;
    }

    std::unordered_set<PageId> seenPageIds;
    std::unordered_set<RegionId> allRegionIds;

    for (size_t i = 0; i < m_pages.size(); ++i) {
        const auto& page = m_pages[i];
        if (!page.isValid()) {
            return false;
        }
        if (!seenPageIds.insert(page.id()).second) {
            return false;
        }
        if (page.orderIndex() != static_cast<int>(i)) {
            return false;
        }
        for (const auto& region : page.regions()) {
            if (!allRegionIds.insert(region.id()).second) {
                return false;
            }
        }
    }

    std::unordered_set<GlossaryId> seenGlossaryIds;
    for (const auto& g : m_glossary) {
        if (!g.isValid() || !seenGlossaryIds.insert(g.id).second) {
            return false;
        }
    }

    return true;
}

bool ComicProject::operator==(const ComicProject& other) const noexcept {
    return m_id == other.m_id && m_name == other.m_name && m_projectVersion == other.m_projectVersion &&
           m_schemaVersion == other.m_schemaVersion && m_formatVersion == other.m_formatVersion &&
           m_sourceLanguage == other.m_sourceLanguage && m_targetLanguage == other.m_targetLanguage &&
           m_readingDirection == other.m_readingDirection && m_pages == other.m_pages &&
           m_settings == other.m_settings && m_glossary == other.m_glossary;
}

} // namespace manga::domain
