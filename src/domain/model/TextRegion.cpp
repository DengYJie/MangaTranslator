#include "domain/model/TextRegion.h"

namespace manga::domain {

void TextRegion::setGeometry(RectGeometry geometry) {
    m_geometry = geometry;
    ++m_contentRevision;
}

void TextRegion::setSource(TextContent source) {
    m_source = std::move(source);
    ++m_contentRevision;
}

void TextRegion::setTranslation(TranslationEntry translation) {
    m_translation = std::move(translation);
    ++m_contentRevision;
}

void TextRegion::setLayout(TextLayout layout) {
    m_layout = std::move(layout);
    ++m_contentRevision;
}

void TextRegion::setClear(ClearSettings clear) {
    m_clear = std::move(clear);
    ++m_contentRevision;
}

bool TextRegion::isValid() const noexcept {
    return !m_id.value.isNil() && m_orderIndex >= 0 && m_geometry.isValid() && m_source.isValid() &&
           m_layout.isValid() && m_clear.isValid();
}

bool TextRegion::operator==(const TextRegion& other) const noexcept {
    return m_id == other.m_id && m_geometry == other.m_geometry && m_orderIndex == other.m_orderIndex &&
           m_source == other.m_source && m_translation == other.m_translation && m_layout == other.m_layout &&
           m_clear == other.m_clear && m_enabled == other.m_enabled && m_locked == other.m_locked &&
           m_translationVisible == other.m_translationVisible && m_isCleared == other.m_isCleared &&
           m_contentRevision == other.m_contentRevision;
}

} // namespace manga::domain
