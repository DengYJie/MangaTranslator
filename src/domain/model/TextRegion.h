#pragma once

#include "domain/model/ClearSettings.h"
#include "domain/model/Geometry.h"
#include "domain/model/TextContent.h"
#include "domain/model/TextLayout.h"
#include "domain/model/TranslationEntry.h"
#include "domain/model/Uuid.h"

#include <cstdint>
#include <functional>

namespace manga::domain {

/// A text bubble / speech region on a comic page.
class TextRegion {
public:
    TextRegion() : m_id(RegionId::generate()) {}
    explicit TextRegion(RegionId id) : m_id(id) {}
    TextRegion(RegionId id, RectGeometry geometry, int orderIndex = 0)
        : m_id(id), m_geometry(geometry), m_orderIndex(orderIndex) {}
    TextRegion(RegionId id,
               RectGeometry geometry,
               std::optional<RectGeometry> bubbleGeometry,
               int orderIndex = 0,
               std::optional<PolygonGeometry> bubblePolygon = std::nullopt)
        : m_id(id), m_geometry(geometry), m_bubbleGeometry(bubbleGeometry), m_orderIndex(orderIndex),
          m_bubblePolygon(std::move(bubblePolygon)) {}

    [[nodiscard]] const RegionId& id() const noexcept {
        return m_id;
    }
    [[nodiscard]] const RectGeometry& geometry() const noexcept {
        return m_geometry;
    }
    [[nodiscard]] const std::optional<RectGeometry>& bubbleGeometry() const noexcept {
        return m_bubbleGeometry;
    }
    [[nodiscard]] const std::optional<PolygonGeometry>& bubblePolygon() const noexcept {
        return m_bubblePolygon;
    }
    [[nodiscard]] int orderIndex() const noexcept {
        return m_orderIndex;
    }

    [[nodiscard]] const TextContent& source() const noexcept {
        return m_source;
    }
    [[nodiscard]] const TranslationEntry& translation() const noexcept {
        return m_translation;
    }
    [[nodiscard]] const TextLayout& layout() const noexcept {
        return m_layout;
    }
    [[nodiscard]] const ClearSettings& clear() const noexcept {
        return m_clear;
    }

    [[nodiscard]] bool isEnabled() const noexcept {
        return m_enabled;
    }
    [[nodiscard]] bool isLocked() const noexcept {
        return m_locked;
    }
    [[nodiscard]] bool isTranslationVisible() const noexcept {
        return m_translationVisible;
    }
    [[nodiscard]] bool isCleared() const noexcept {
        return m_isCleared;
    }

    [[nodiscard]] uint64_t contentRevision() const noexcept {
        return m_contentRevision;
    }

    // Controlled mutators. All content modifications strictly increment m_contentRevision
    void setGeometry(RectGeometry geometry);
    void setBubbleGeometry(std::optional<RectGeometry> geometry);
    void setBubblePolygon(std::optional<PolygonGeometry> polygon);
    void setOrderIndex(int index) noexcept {
        m_orderIndex = index;
    }

    void setSource(TextContent source);
    void setTranslation(TranslationEntry translation);
    void setLayout(TextLayout layout);
    void setClear(ClearSettings clear);

    template <typename Func> void updateSource(Func&& fn) {
        fn(m_source);
        ++m_contentRevision;
    }

    template <typename Func> void updateTranslation(Func&& fn) {
        fn(m_translation);
        ++m_contentRevision;
    }

    template <typename Func> void updateLayout(Func&& fn) {
        fn(m_layout);
        ++m_contentRevision;
    }

    template <typename Func> void updateClear(Func&& fn) {
        fn(m_clear);
        ++m_contentRevision;
    }

    void setEnabled(bool enabled) noexcept {
        m_enabled = enabled;
    }
    void setLocked(bool locked) noexcept {
        m_locked = locked;
    }
    void setTranslationVisible(bool visible) noexcept {
        m_translationVisible = visible;
    }
    void setIsCleared(bool cleared) noexcept {
        m_isCleared = cleared;
    }
    void setContentRevision(uint64_t revision) noexcept {
        m_contentRevision = revision;
    }

    void bumpRevision() noexcept {
        ++m_contentRevision;
    }

    /// Validates internal consistency of the region.
    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] bool operator==(const TextRegion& other) const noexcept;

private:
    RegionId m_id;
    RectGeometry m_geometry;
    std::optional<RectGeometry> m_bubbleGeometry;
    std::optional<PolygonGeometry> m_bubblePolygon;
    int m_orderIndex{0};

    TextContent m_source;
    TranslationEntry m_translation;
    TextLayout m_layout;
    ClearSettings m_clear;

    bool m_enabled{true};
    bool m_locked{false};
    bool m_translationVisible{true};
    bool m_isCleared{false};

    uint64_t m_contentRevision{0};
};

} // namespace manga::domain
