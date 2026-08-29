#include "TextUseCases.h"

#include <QHash>
#include <QSet>

#include <cmath>

namespace manga::application {
namespace {

[[nodiscard]] ApplicationError error(ErrorCategory category, QString code, QString message) {
    return makeApplicationError(category, std::move(code), std::move(message));
}

[[nodiscard]] Result<const domain::TextRegion*>
findRegion(const domain::ComicProject* project, const domain::PageId& pageId, const domain::RegionId& regionId) {
    if (project == nullptr) {
        return std::unexpected(error(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    const auto* page = project->findPage(pageId);
    if (page == nullptr) {
        return std::unexpected(error(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
    }
    const auto* region = page->findRegion(regionId);
    if (region == nullptr) {
        return std::unexpected(error(ErrorCategory::Input, "input.region_not_found", "Region was not found"));
    }
    return region;
}

[[nodiscard]] Result<void>
applyEdit(domain::ComicProject* project, const domain::PageId& pageId, domain::PageRegionEdit edit) {
    const domain::PageRegionEdit edits[] = {std::move(edit)};
    const auto result = project->applyPageRegionEdits(pageId, edits);
    if (!result) {
        return std::unexpected(error(ErrorCategory::Input, "input.edit_rejected", "Region edit was rejected"));
    }
    return {};
}

} // namespace

Result<void> EditSourceTextUseCase::execute(const EditSourceTextRequest& request) {
    if (request.text.isEmpty()) {
        return std::unexpected(error(ErrorCategory::Input, "input.empty_text", "Source text cannot be empty"));
    }
    const auto found = findRegion(m_project, request.pageId, request.regionId);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->contentRevision() != request.expectedRevision) {
        return std::unexpected(error(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
    }

    auto source = (*found)->source();
    source.setManualText(request.text.toStdString());
    return applyEdit(m_project, request.pageId, domain::PageRegionEdit{.regionId = request.regionId, .source = source});
}

Result<void> EditTranslationUseCase::execute(const EditTranslationRequest& request) {
    const auto found = findRegion(m_project, request.pageId, request.regionId);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->contentRevision() != request.expectedRevision) {
        return std::unexpected(error(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
    }

    auto translation = (*found)->translation();
    translation.setManualText(request.translation.toStdString());
    return applyEdit(
        m_project, request.pageId, domain::PageRegionEdit{.regionId = request.regionId, .translation = translation});
}

Result<void> SelectCandidateUseCase::execute(const SelectCandidateRequest& request) {
    const auto found = findRegion(m_project, request.pageId, request.regionId);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->contentRevision() != request.expectedRevision) {
        return std::unexpected(error(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
    }

    auto translation = (*found)->translation();
    if (!translation.selectCandidate(request.candidateId.toStdString())) {
        return std::unexpected(
            error(ErrorCategory::Input, "input.candidate_not_found", "Translation candidate was not found"));
    }
    return applyEdit(
        m_project, request.pageId, domain::PageRegionEdit{.regionId = request.regionId, .translation = translation});
}

Result<void> ApplyOcrResultUseCase::execute(const ApplyOcrResultRequest& request) {
    if (m_project == nullptr) {
        return std::unexpected(error(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    if (m_project->id() != request.projectId) {
        return std::unexpected(error(ErrorCategory::Project, "project.mismatch", "Project changed"));
    }
    if (request.generation != request.currentGeneration) {
        return std::unexpected(error(ErrorCategory::Cancelled, "cancelled.stale_generation", "OCR result is stale"));
    }
    if (!request.expectedRegionId.isValid() || request.result.regionId != request.expectedRegionId) {
        return std::unexpected(
            error(ErrorCategory::Protocol, "protocol.region_mismatch", "OCR response region does not match request"));
    }
    if (request.result.text.isEmpty() || !std::isfinite(request.result.confidence) || request.result.confidence < 0.0 ||
        request.result.confidence > 1.0) {
        return std::unexpected(error(ErrorCategory::Protocol, "protocol.invalid_ocr_result", "OCR result is invalid"));
    }

    const auto found = findRegion(m_project, request.pageId, request.result.regionId);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->contentRevision() != request.expectedRevision) {
        return std::unexpected(error(ErrorCategory::Input, "input.stale_revision", "Region content changed"));
    }
    const auto& currentSource = (*found)->source();
    if (currentSource.sourceType != domain::TextSourceType::OcrAuto && !currentSource.text.empty()) {
        return std::unexpected(error(ErrorCategory::Input, "input.manual_override", "Manual source text exists"));
    }

    auto source = currentSource;
    source.setOcrResult(request.result.text.toStdString(), request.result.confidence);
    return applyEdit(
        m_project, request.pageId, domain::PageRegionEdit{.regionId = request.result.regionId, .source = source});
}

Result<TranslationAdoptionResult> ApplyTranslationResultUseCase::execute(const ApplyTranslationResultRequest& request) {
    if (m_project == nullptr) {
        return std::unexpected(error(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
    }
    if (m_project->id() != request.projectId) {
        return std::unexpected(error(ErrorCategory::Project, "project.mismatch", "Project changed"));
    }
    if (request.generation != request.currentGeneration) {
        return std::unexpected(
            error(ErrorCategory::Cancelled, "cancelled.stale_generation", "Translation result is stale"));
    }
    if (request.expectedRegions.isEmpty()) {
        return std::unexpected(
            error(ErrorCategory::Input, "input.empty_translation_targets", "Translation targets are required"));
    }

    const auto* page = m_project->findPage(request.pageId);
    if (page == nullptr) {
        return std::unexpected(error(ErrorCategory::Project, "project.page_not_found", "Page was not found"));
    }

    QSet<QString> responseIds;
    for (const auto& item : request.result.items) {
        const auto key = QString::fromStdString(item.regionId.toString());
        if (responseIds.contains(key)) {
            return std::unexpected(error(ErrorCategory::Protocol,
                                         "protocol.duplicate_id",
                                         "Translation response contains duplicate region IDs"));
        }
        responseIds.insert(key);
    }

    QHash<QString, TranslationTargetSnapshot> expectedById;
    for (const auto& expected : request.expectedRegions) {
        const auto key = QString::fromStdString(expected.regionId.toString());
        if (expectedById.contains(key)) {
            return std::unexpected(error(
                ErrorCategory::Input, "input.duplicate_target", "Translation targets contain duplicate region IDs"));
        }
        expectedById.insert(key, expected);
    }

    TranslationAdoptionResult adoption;
    std::vector<domain::PageRegionEdit> edits;
    QSet<QString> matchedExpectedIds;
    for (const auto& item : request.result.items) {
        const auto key = QString::fromStdString(item.regionId.toString());
        const auto expectedIt = expectedById.constFind(key);
        if (expectedIt == expectedById.cend()) {
            adoption.failures.append({item.regionId, "protocol.unknown_id"});
            continue;
        }
        matchedExpectedIds.insert(key);

        const auto* region = page->findRegion(item.regionId);
        if (region == nullptr) {
            adoption.failures.append({item.regionId, "input.region_not_found"});
            continue;
        }
        if (region->contentRevision() != expectedIt->contentRevision) {
            adoption.failures.append({item.regionId, "input.stale_revision"});
            continue;
        }
        if (region->translation().hasManualText()) {
            adoption.failures.append({item.regionId, "input.manual_override"});
            continue;
        }
        if (item.sourceText != expectedIt->sourceText ||
            item.sourceText != QString::fromStdString(region->source().text)) {
            adoption.failures.append({item.regionId, "protocol.source_mismatch"});
            continue;
        }

        std::vector<domain::TranslationCandidate> candidates;
        candidates.reserve(static_cast<std::size_t>(item.candidates.size()));
        for (qsizetype index = 0; index < item.candidates.size(); ++index) {
            candidates.push_back(domain::TranslationCandidate{
                .id = item.regionId.toString() + "-" + std::to_string(index),
                .text = item.candidates[index].toStdString(),
                .provider = request.result.providerId.toStdString(),
            });
        }
        auto translation = region->translation();
        translation.setModelResult(item.translation.toStdString(), std::move(candidates));
        edits.push_back(domain::PageRegionEdit{.regionId = item.regionId, .translation = std::move(translation)});
        adoption.appliedRegionIds.append(item.regionId);
    }

    for (auto it = expectedById.cbegin(); it != expectedById.cend(); ++it) {
        if (!matchedExpectedIds.contains(it.key())) {
            adoption.failures.append({it->regionId, "protocol.missing_id"});
        }
    }

    if (!edits.empty()) {
        const auto result = m_project->applyPageRegionEdits(request.pageId, edits);
        if (!result) {
            return std::unexpected(
                error(ErrorCategory::Input, "input.edit_rejected", "Translation result could not be applied"));
        }
    }
    return adoption;
}

} // namespace manga::application
