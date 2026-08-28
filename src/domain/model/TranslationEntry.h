#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace manga::domain {

/// A candidate translation option returned by an LLM or translation engine.
struct TranslationCandidate {
    std::string id;
    std::string text;
    double confidence{1.0};
    std::string provider;

    [[nodiscard]] bool operator==(const TranslationCandidate& other) const noexcept {
        return id == other.id && text == other.text && std::abs(confidence - other.confidence) <= 1e-5 &&
               provider == other.provider;
    }
};

/// Translation entry encapsulating model output, candidates, candidate selection, and manual override.
class TranslationEntry {
public:
    TranslationEntry() = default;

    /// Returns the effective translation according to the strict priority rules:
    /// 1. manualText (if present and non-empty)
    /// 2. selectedCandidate (if selectedCandidateId matches an existing candidate)
    /// 3. modelText (if present)
    /// 4. empty string
    [[nodiscard]] std::string effectiveText() const;

    /// Checks if manual override is active.
    [[nodiscard]] bool hasManualText() const noexcept {
        return m_manualText.has_value() && !m_manualText->empty();
    }

    [[nodiscard]] const std::optional<std::string>& manualText() const noexcept {
        return m_manualText;
    }

    [[nodiscard]] const std::optional<std::string>& modelText() const noexcept {
        return m_modelText;
    }

    [[nodiscard]] const std::vector<TranslationCandidate>& candidates() const noexcept {
        return m_candidates;
    }

    [[nodiscard]] const std::optional<std::string>& selectedCandidateId() const noexcept {
        return m_selectedCandidateId;
    }

    /// Finds and returns the currently selected candidate, if any.
    [[nodiscard]] std::optional<TranslationCandidate> selectedCandidate() const;

    /// Checks if the candidate ID exists in the candidate list.
    [[nodiscard]] bool hasCandidate(const std::string& candidateId) const noexcept;

    // Mutators
    void setManualText(std::string text);
    void clearManualText() noexcept;

    void setModelResult(std::string text, std::vector<TranslationCandidate> candidates = {});
    void addCandidate(TranslationCandidate candidate);

    bool selectCandidate(const std::string& candidateId);
    void clearSelectedCandidate() noexcept;

    void reset() noexcept;

    [[nodiscard]] bool operator==(const TranslationEntry& other) const noexcept = default;

private:
    std::optional<std::string> m_modelText;
    std::vector<TranslationCandidate> m_candidates;
    std::optional<std::string> m_selectedCandidateId;
    std::optional<std::string> m_manualText;
};

} // namespace manga::domain
