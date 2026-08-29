#pragma once

#include "application/dto/OcrDto.h"
#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "domain/model/ComicProject.h"

namespace manga::application {

struct EditSourceTextRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QString text;
    quint64 expectedRevision{0};
};

struct EditTranslationRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QString translation;
    quint64 expectedRevision{0};
};

struct SelectCandidateRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QString candidateId;
    quint64 expectedRevision{0};
};

struct ApplyOcrResultRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId expectedRegionId;
    OcrResponse result;
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    quint64 expectedRevision{0};
};

struct TranslationTargetSnapshot {
    domain::RegionId regionId;
    QString sourceText;
    quint64 contentRevision{0};
};

struct TranslationAdoptionFailure {
    domain::RegionId regionId;
    QString code;
};

struct TranslationAdoptionResult {
    QVector<domain::RegionId> appliedRegionIds;
    QVector<TranslationAdoptionFailure> failures;
};

struct ApplyTranslationResultRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    TranslationResponse result;
    quint64 sessionGeneration{0};
    quint64 currentSessionGeneration{0};
    QVector<TranslationTargetSnapshot> expectedRegions;
};

class EditSourceTextUseCase {
public:
    explicit EditSourceTextUseCase(domain::ComicProject* project) : m_project(project) {}
    [[nodiscard]] Result<void> execute(const EditSourceTextRequest& request);

private:
    domain::ComicProject* m_project;
};

class EditTranslationUseCase {
public:
    explicit EditTranslationUseCase(domain::ComicProject* project) : m_project(project) {}
    [[nodiscard]] Result<void> execute(const EditTranslationRequest& request);

private:
    domain::ComicProject* m_project;
};

class SelectCandidateUseCase {
public:
    explicit SelectCandidateUseCase(domain::ComicProject* project) : m_project(project) {}
    [[nodiscard]] Result<void> execute(const SelectCandidateRequest& request);

private:
    domain::ComicProject* m_project;
};

class ApplyOcrResultUseCase {
public:
    explicit ApplyOcrResultUseCase(domain::ComicProject* project) : m_project(project) {}
    [[nodiscard]] Result<void> execute(const ApplyOcrResultRequest& request);

private:
    domain::ComicProject* m_project;
};

class ApplyTranslationResultUseCase {
public:
    explicit ApplyTranslationResultUseCase(domain::ComicProject* project) : m_project(project) {}
    [[nodiscard]] Result<TranslationAdoptionResult> execute(const ApplyTranslationResultRequest& request);

private:
    domain::ComicProject* m_project;
};

} // namespace manga::application
