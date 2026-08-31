#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"

namespace manga::application {

PrepareProcessingAdoptionUseCase::PrepareProcessingAdoptionUseCase(domain::ComicProject* project,
                                                                   PrepareClearResultUseCase* prepareClearUseCase)
    : m_project(project), m_prepareClearUseCase(prepareClearUseCase) {}

QFuture<Result<PreparedProcessingAdoption>>
PrepareProcessingAdoptionUseCase::execute(const PrepareProcessingAdoptionRequest& request) {
    if (!m_project) {
        return makeFailedFuture<PreparedProcessingAdoption>(detail::applyError(
            ErrorCategory::Internal, QStringLiteral("internal.null_project"), QStringLiteral("Project is null")));
    }

    if (m_project->id() != request.projectId) {
        return makeFailedFuture<PreparedProcessingAdoption>(detail::applyError(
            ErrorCategory::Project, QStringLiteral("project.mismatch"), QStringLiteral("Project changed")));
    }

    if (request.sessionGeneration != request.currentSessionGeneration) {
        return makeFailedFuture<PreparedProcessingAdoption>(detail::applyError(
            ErrorCategory::Cancelled, QStringLiteral("cancelled.stale_generation"), QStringLiteral("Result is stale")));
    }

    const auto* page = m_project->findPage(request.pageId);
    if (!page) {
        return makeFailedFuture<PreparedProcessingAdoption>(detail::applyError(
            ErrorCategory::Project, QStringLiteral("project.page_not_found"), QStringLiteral("Page was not found")));
    }

    auto checkRegion = [&](const domain::RegionId& rId, quint64 contentRevision) -> Result<const domain::TextRegion*> {
        const auto* region = page->findRegion(rId);
        if (!region) {
            return std::unexpected(detail::applyError(ErrorCategory::Project,
                                                      QStringLiteral("project.region_not_found"),
                                                      QStringLiteral("Region not found")));
        }
        if (region->contentRevision() != contentRevision) {
            return std::unexpected(detail::applyError(
                ErrorCategory::Input, QStringLiteral("input.stale_revision"), QStringLiteral("Stale revision")));
        }
        if (region->isLocked()) {
            return std::unexpected(detail::applyError(
                ErrorCategory::Input, QStringLiteral("input.manual_override"), QStringLiteral("Manual override")));
        }
        return region;
    };

    PreparedProcessingAdoption prepared;
    prepared.stage = request.stage;

    if (request.stage == OperationStage::Ocr) {
        for (const auto& ocr : request.ocr) {
            quint64 expectedRev = request.expectedRevisions.value(ocr.regionId, 0);
            auto r = checkRegion(ocr.regionId, expectedRev);
            if (!r) {
                ProcessingIssue issue;
                issue.regionId = ocr.regionId;
                issue.stage = QStringLiteral("ocr");
                issue.error = r.error();
                prepared.skipped.append(issue);
                continue;
            }

            const auto* region = *r;
            const auto& src = region->source();
            const bool isManual = (src.sourceType == domain::TextSourceType::OcrManualModified ||
                                   (src.sourceType == domain::TextSourceType::PureManual && !src.text.empty()));
            if (isManual && request.replaceManualPolicy != ReplaceManualPolicy::Replace) {
                ProcessingIssue issue;
                issue.regionId = ocr.regionId;
                issue.stage = QStringLiteral("ocr");
                issue.error = detail::applyError(
                    ErrorCategory::Input, QStringLiteral("input.manual_override"), QStringLiteral("Manual override"));
                prepared.skipped.append(issue);
                continue;
            }

            PreparedRegionChange change;
            change.pageId = request.pageId;
            change.regionId = ocr.regionId;
            change.expectedRevision = expectedRev;
            change.before.regionId = ocr.regionId;
            change.before.source = region->source();

            change.after.regionId = ocr.regionId;
            change.after.source = region->source();
            if (!change.after.source) {
                change.after.source = domain::TextContent();
            }
            change.after.source->setOcrResult(ocr.text.toStdString(), ocr.confidence);

            prepared.changes.append(change);
        }
        return makeReadyFuture<PreparedProcessingAdoption>(prepared);
    } else if (request.stage == OperationStage::Translate) {
        if (request.translation.has_value()) {
            QHash<QString, int> translationIdCounts;
            for (const auto& item : request.translation->items) {
                translationIdCounts[QString::fromStdString(item.regionId.toString())]++;
            }

            for (const auto& item : request.translation->items) {
                if (translationIdCounts[QString::fromStdString(item.regionId.toString())] > 1) {
                    ProcessingIssue issue;
                    issue.regionId = item.regionId;
                    issue.stage = QStringLiteral("translate");
                    issue.error = detail::applyError(ErrorCategory::Protocol,
                                                     QStringLiteral("protocol.duplicate_id"),
                                                     QStringLiteral("Duplicate ID"));
                    prepared.skipped.append(issue);
                    continue;
                }

                quint64 expectedRev = request.expectedRevisions.value(item.regionId, 0);
                auto r = checkRegion(item.regionId, expectedRev);
                if (!r) {
                    ProcessingIssue issue;
                    issue.regionId = item.regionId;
                    issue.stage = QStringLiteral("translate");
                    issue.error = r.error();
                    prepared.skipped.append(issue);
                    continue;
                }

                const auto* region = *r;
                if (region->translation().hasManualText() &&
                    request.replaceManualPolicy != ReplaceManualPolicy::Replace) {
                    ProcessingIssue issue;
                    issue.regionId = item.regionId;
                    issue.stage = QStringLiteral("translate");
                    issue.error = detail::applyError(ErrorCategory::Input,
                                                     QStringLiteral("input.manual_override"),
                                                     QStringLiteral("Manual override"));
                    prepared.skipped.append(issue);
                    continue;
                }

                PreparedRegionChange change;
                change.pageId = request.pageId;
                change.regionId = item.regionId;
                change.expectedRevision = expectedRev;
                change.before.regionId = item.regionId;
                change.before.translation = region->translation();

                change.after.regionId = item.regionId;
                change.after.translation = region->translation();
                std::vector<domain::TranslationCandidate> candidates;
                for (const auto& candText : item.candidates) {
                    domain::TranslationCandidate cand;
                    cand.id = domain::Uuid::generateV4().toString();
                    cand.text = candText.toStdString();
                    cand.provider = request.translation->providerId.toStdString();
                    candidates.push_back(cand);
                }
                if (!change.after.translation) {
                    change.after.translation = domain::TranslationEntry();
                }
                change.after.translation->setModelResult(item.translation.toStdString(), candidates);
                if (request.replaceManualPolicy == ReplaceManualPolicy::Replace) {
                    change.after.translation->clearManualText();
                }

                prepared.changes.append(change);
            }
        }
        return makeReadyFuture<PreparedProcessingAdoption>(prepared);
    } else if (request.stage == OperationStage::Layout) {
        for (const auto& item : request.layout) {
            quint64 expectedRev = request.expectedRevisions.value(item.regionId, 0);
            auto r = checkRegion(item.regionId, expectedRev);
            if (!r) {
                ProcessingIssue issue;
                issue.regionId = item.regionId;
                issue.stage = QStringLiteral("layout");
                issue.error = r.error();
                prepared.skipped.append(issue);
                continue;
            }

            if (!item.resolvedLayout.isValid()) {
                ProcessingIssue issue;
                issue.regionId = item.regionId;
                issue.stage = QStringLiteral("layout");
                issue.error = detail::applyError(ErrorCategory::Protocol,
                                                 QStringLiteral("protocol.invalid_layout_result"),
                                                 QStringLiteral("Layout result is invalid"));
                prepared.skipped.append(issue);
                continue;
            }

            PreparedRegionChange change;
            change.pageId = request.pageId;
            change.regionId = item.regionId;
            change.expectedRevision = expectedRev;
            change.before.regionId = item.regionId;
            change.before.layout = (*r)->layout();

            change.after.regionId = item.regionId;
            change.after.layout = item.resolvedLayout;

            prepared.changes.append(change);

            for (const auto& warning : item.warnings) {
                ProcessingIssue issue;
                issue.regionId = item.regionId;
                issue.stage = QStringLiteral("layout");
                issue.error = detail::applyError(ErrorCategory::Input, warning.code, warning.safeMessage);
                prepared.skipped.append(issue);
            }
        }
        return makeReadyFuture<PreparedProcessingAdoption>(prepared);
    } else if (request.stage == OperationStage::Clear) {
        if (!request.clear.isEmpty()) {
            const auto& clearData = request.clear.first();
            quint64 expectedRev = request.expectedRevisions.value(
                clearData.regionId,
                page->findRegion(clearData.regionId) ? page->findRegion(clearData.regionId)->contentRevision() : 0);
            auto r = checkRegion(clearData.regionId, expectedRev);
            if (!r) {
                ProcessingIssue issue;
                issue.regionId = clearData.regionId;
                issue.stage = QStringLiteral("clear");
                issue.error = r.error();
                prepared.skipped.append(issue);
                return makeReadyFuture<PreparedProcessingAdoption>(prepared);
            }

            PrepareClearResultRequest req;
            req.projectId = request.projectId;
            req.pageId = request.pageId;
            req.expectedRegionId = clearData.regionId;
            req.pageSize = request.pageSize;
            req.projectRoot = request.projectRoot;
            req.result = clearData;
            req.sessionGeneration = request.sessionGeneration;
            req.currentSessionGeneration = request.currentSessionGeneration;
            req.expectedRevision = expectedRev;
            req.context = request.context;

            if (!m_prepareClearUseCase) {
                return makeFailedFuture<PreparedProcessingAdoption>(
                    detail::applyError(ErrorCategory::Internal,
                                       QStringLiteral("internal.null_clear_use_case"),
                                       QStringLiteral("Clear UseCase is null")));
            }

            auto promise = std::make_shared<QPromise<Result<PreparedProcessingAdoption>>>();
            promise->start();
            auto future = promise->future();

            m_prepareClearUseCase->execute(req).then([promise, prepared](Result<PreparedClearChange> result) mutable {
                if (!result) {
                    promise->addResult(std::unexpected(result.error()));
                } else {
                    prepared.clearAssetChange = *result;
                    promise->addResult(prepared);
                }
                promise->finish();
            });
            return future;
        }
        return makeReadyFuture<PreparedProcessingAdoption>(prepared);
    } else {
        return makeReadyFuture<PreparedProcessingAdoption>(prepared);
    }
}

} // namespace manga::application
