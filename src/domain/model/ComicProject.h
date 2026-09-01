#pragma once

#include "domain/model/ComicPage.h"
#include "domain/model/GlossaryEntry.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/ProjectSettings.h"
#include "domain/model/ReadingDirection.h"
#include "domain/model/Uuid.h"
#include "domain/validation/ValidationResult.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace manga::domain {

/// Edit descriptor for a single region within a batch transaction.
struct PageRegionEdit {
    RegionId regionId;
    std::optional<RectGeometry> geometry;
    std::optional<TextContent> source;
    std::optional<TranslationEntry> translation;
    std::optional<TextLayout> layout;
};

/// Root aggregate entity representing a manga translation project.
class ComicProject {
public:
    static constexpr int kCurrentSchemaVersion = 1;
    static constexpr int kCurrentFormatVersion = 1;
    static constexpr std::string_view kCurrentProjectVersion = "0.1.0";

    ComicProject();
    explicit ComicProject(std::string name, ProjectId id = ProjectId::generate());

    [[nodiscard]] const ProjectId& id() const noexcept {
        return m_id;
    }
    [[nodiscard]] const std::string& name() const noexcept {
        return m_name;
    }
    [[nodiscard]] const std::string& projectVersion() const noexcept {
        return m_projectVersion;
    }
    [[nodiscard]] int schemaVersion() const noexcept {
        return m_schemaVersion;
    }
    [[nodiscard]] int formatVersion() const noexcept {
        return m_formatVersion;
    }

    /// Runtime dirty state: not persisted to disk. Managed by the application layer (load/save).
    [[nodiscard]] bool isDirty() const noexcept {
        return m_isDirty;
    }
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return m_revision;
    }

    [[nodiscard]] const LanguageCode& sourceLanguage() const noexcept {
        return m_sourceLanguage;
    }
    [[nodiscard]] const LanguageCode& targetLanguage() const noexcept {
        return m_targetLanguage;
    }
    [[nodiscard]] ReadingDirection readingDirection() const noexcept {
        return m_readingDirection;
    }

    [[nodiscard]] const std::vector<ComicPage>& pages() const noexcept {
        return m_pages;
    }
    [[nodiscard]] const ProjectSettings& settings() const noexcept {
        return m_settings;
    }
    [[nodiscard]] const std::vector<GlossaryEntry>& glossary() const noexcept {
        return m_glossary;
    }

    // Project Metadata Mutators
    void setName(std::string name) {
        m_name = std::move(name);
        markDirty();
    }
    void setProjectVersion(std::string version) {
        m_projectVersion = std::move(version);
    }
    void setSchemaVersion(int version) noexcept {
        m_schemaVersion = version;
    }
    void setFormatVersion(int version) noexcept {
        m_formatVersion = version;
    }

    void markDirty() noexcept {
        m_isDirty = true;
        ++m_revision;
    }
    void markClean() noexcept {
        m_isDirty = false;
    }
    [[nodiscard]] bool markCleanIfRevision(std::uint64_t savedRevision) noexcept {
        if (m_revision != savedRevision) {
            return false;
        }
        m_isDirty = false;
        return true;
    }

    void setSourceLanguage(LanguageCode lang) noexcept {
        m_sourceLanguage = std::move(lang);
        markDirty();
    }
    void setTargetLanguage(LanguageCode lang) noexcept {
        m_targetLanguage = std::move(lang);
        markDirty();
    }
    void setReadingDirection(ReadingDirection dir) noexcept {
        m_readingDirection = dir;
        markDirty();
    }
    void setSettings(ProjectSettings settings) {
        m_settings = std::move(settings);
        markDirty();
    }

    // Page Lifecycle

    [[nodiscard]] bool hasPage(const PageId& pageId) const noexcept;
    [[nodiscard]] const ComicPage* findPage(const PageId& pageId) const noexcept;

    /// Runtime: adds a page to the project.
    /// Validates uniqueness, page.isValid(). Automatically assigns contiguous orderIndex.
    bool addPage(ComicPage page);

    /// Removes a page by ID and re-normalizes orderIndex to contiguous 0..N-1.
    bool removePage(const PageId& pageId);

    void normalizePageOrder() noexcept;

    // Region Lifecycle (project-level, maintains cross-page RegionId uniqueness)

    /// Adds a region to a specific page. Validates project-wide RegionId uniqueness.
    [[nodiscard]] std::expected<void, ProjectEditError> addRegionToPage(const PageId& pageId, TextRegion region);

    /// Inserts a region at a specific order position on a page.
    /// Checks project-wide RegionId uniqueness, then inserts atomically, then marks dirty once.
    [[nodiscard]] std::expected<void, ProjectEditError>
    insertRegionOnPage(const PageId& pageId, TextRegion region, int orderIndex);

    /// Removes a region from a specific page.
    [[nodiscard]] std::expected<void, ProjectEditError> removeRegionFromPage(const PageId& pageId,
                                                                             const RegionId& regionId);

    // Region Content Mutations (project-level, marks Dirty)

    /// Updates region geometry on a specific page (clamps to bounds).
    [[nodiscard]] std::expected<RectGeometry, ProjectEditError>
    updateRegionGeometryOnPage(const PageId& pageId, const RegionId& regionId, const RectGeometry& newGeometry);

    /// Updates region translation on a specific page.
    [[nodiscard]] std::expected<void, ProjectEditError>
    setRegionTranslationOnPage(const PageId& pageId, const RegionId& regionId, TranslationEntry translation);

    [[nodiscard]] std::expected<void, ProjectEditError>
    reorderRegionOnPage(const PageId& pageId, const RegionId& regionId, int newOrderIndex);

    /// Applies a batch of region edits to a single page atomically (snapshot/rollback).
    [[nodiscard]] std::expected<void, ProjectEditError> applyPageRegionEdits(const PageId& pageId,
                                                                             std::span<const PageRegionEdit> edits);

    /// Atomic clear adoption: sets the page clear layer reference and marks the region
    /// cleared in one validation pass with one markDirty. The layer path must already be a
    /// committed, project-relative content-addressed reference.
    [[nodiscard]] std::expected<void, ProjectEditError> applyClearChangeOnPage(const PageId& pageId,
                                                                               const RegionId& regionId,
                                                                               std::optional<std::string> layerPath,
                                                                               bool isCleared);

    /// Atomically replaces the page-level clear-layer reference and marks the project dirty.
    [[nodiscard]] std::expected<void, ProjectEditError> setClearLayerAssetOnPage(const PageId& pageId,
                                                                                 std::optional<std::string> layerPath);

    // Glossary

    bool addGlossaryEntry(GlossaryEntry entry);
    bool removeGlossaryEntry(const GlossaryId& id);

    // Validation

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool operator==(const ComicProject& other) const noexcept;

private:
    friend class ProjectRehydrationBuilder;

    /// Rehydration (deserialization only): inserts a page without re-assigning orderIndex.
    bool loadPage(ComicPage page);

    [[nodiscard]] bool hasRegionInAnyPage(const RegionId& regionId) const noexcept;
    ComicPage* findPageMutable(const PageId& pageId) noexcept;

    ProjectId m_id;
    std::string m_name{"Untitled"};
    std::string m_projectVersion{kCurrentProjectVersion};
    int m_schemaVersion{kCurrentSchemaVersion};
    int m_formatVersion{kCurrentFormatVersion};

    bool m_isDirty{false};
    std::uint64_t m_revision{0};

    LanguageCode m_sourceLanguage{languages::Japanese};
    LanguageCode m_targetLanguage{languages::SimplifiedChinese};
    ReadingDirection m_readingDirection{ReadingDirection::RightToLeft};

    std::vector<ComicPage> m_pages;
    ProjectSettings m_settings;
    std::vector<GlossaryEntry> m_glossary;
};

} // namespace manga::domain
