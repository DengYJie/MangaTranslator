#include "domain/model/TranslationEntry.h"

#include <algorithm>

namespace manga::domain {

std::string TranslationEntry::effectiveText() const {
    if (m_manualText.has_value() && !m_manualText->empty()) {
        return *m_manualText;
    }

    if (m_selectedCandidateId.has_value() && !m_selectedCandidateId->empty()) {
        const auto it = std::ranges::find_if(m_candidates, [this](const TranslationCandidate& candidate) {
            return candidate.id == *m_selectedCandidateId;
        });
        if (it != m_candidates.end()) {
            return it->text;
        }
    }

    if (m_modelText.has_value()) {
        return *m_modelText;
    }

    return {};
}

std::optional<TranslationCandidate> TranslationEntry::selectedCandidate() const {
    if (!m_selectedCandidateId.has_value() || m_selectedCandidateId->empty()) {
        return std::nullopt;
    }

    const auto it = std::ranges::find_if(
        m_candidates, [this](const TranslationCandidate& candidate) { return candidate.id == *m_selectedCandidateId; });
    if (it != m_candidates.end()) {
        return *it;
    }
    return std::nullopt;
}

bool TranslationEntry::hasCandidate(const std::string& candidateId) const noexcept {
    return std::ranges::any_of(m_candidates,
                               [&candidateId](const TranslationCandidate& c) { return c.id == candidateId; });
}

void TranslationEntry::setManualText(std::string text) {
    m_manualText = std::move(text);
}

void TranslationEntry::clearManualText() noexcept {
    m_manualText.reset();
}

void TranslationEntry::setModelResult(std::string text, std::vector<TranslationCandidate> candidates) {
    m_modelText = std::move(text);
    m_candidates = std::move(candidates);
    // If selected candidate is no longer present in candidates, clear selection
    if (m_selectedCandidateId.has_value() && !hasCandidate(*m_selectedCandidateId)) {
        m_selectedCandidateId.reset();
    }
}

void TranslationEntry::addCandidate(TranslationCandidate candidate) {
    m_candidates.push_back(std::move(candidate));
}

bool TranslationEntry::selectCandidate(const std::string& candidateId) {
    if (hasCandidate(candidateId)) {
        m_selectedCandidateId = candidateId;
        return true;
    }
    return false;
}

void TranslationEntry::clearSelectedCandidate() noexcept {
    m_selectedCandidateId.reset();
}

void TranslationEntry::reset() noexcept {
    m_modelText.reset();
    m_candidates.clear();
    m_selectedCandidateId.reset();
    m_manualText.reset();
}

} // namespace manga::domain
