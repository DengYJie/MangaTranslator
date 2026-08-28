#include "ExportUseCases.h"

#include "application/operation/AsyncResult.h"

#include <QSet>

namespace manga::application {

QFuture<Result<ExportResponse>> ExportPagesUseCase::execute(const ExportPagesRequest& request,
                                                            const OperationContext& context) {
    if (m_exporter == nullptr || m_project == nullptr) {
        return makeFailedFuture<ExportResponse>(makeApplicationError(
            ErrorCategory::Internal, "internal.unavailable_export_dependency", "Export service is unavailable"));
    }
    if (context.stopToken.stop_requested()) {
        return makeFailedFuture<ExportResponse>(
            makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Export was cancelled"));
    }
    if (request.projectId != m_project->id()) {
        return makeFailedFuture<ExportResponse>(
            makeApplicationError(ErrorCategory::Project, "project.mismatch", "Project changed"));
    }
    if (request.outputDir.trimmed().isEmpty() || request.jpegQuality < 1 || request.jpegQuality > 100 ||
        request.pageSnapshots.isEmpty()) {
        return makeFailedFuture<ExportResponse>(
            makeApplicationError(ErrorCategory::Input, "input.invalid_export", "Export options are invalid"));
    }

    QSet<QString> selectedIds;
    if (request.scope == ExportScope::CurrentPage) {
        if (!request.currentPageId.has_value()) {
            return makeFailedFuture<ExportResponse>(makeApplicationError(
                ErrorCategory::Input, "input.missing_current_page", "A current page is required for export"));
        }
        selectedIds.insert(QString::fromStdString(request.currentPageId->toString()));
    } else if (request.scope == ExportScope::SelectedPages) {
        for (const auto& pageId : request.selectedPageIds) {
            const auto key = QString::fromStdString(pageId.toString());
            if (!pageId.isValid() || selectedIds.contains(key)) {
                return makeFailedFuture<ExportResponse>(makeApplicationError(
                    ErrorCategory::Input, "input.invalid_page_selection", "Selected pages must be valid and unique"));
            }
            selectedIds.insert(key);
        }
        if (selectedIds.isEmpty()) {
            return makeFailedFuture<ExportResponse>(makeApplicationError(
                ErrorCategory::Input, "input.empty_page_selection", "Select at least one page to export"));
        }
    }

    ExportRequest portRequest;
    portRequest.outputDir = request.outputDir;
    portRequest.format = request.format;
    portRequest.jpegQuality = request.jpegQuality;
    QSet<QString> snapshotIds;
    for (const auto& snapshot : request.pageSnapshots) {
        const auto key = QString::fromStdString(snapshot.pageId.toString());
        const auto* page = m_project->findPage(snapshot.pageId);
        if (!snapshot.pageId.isValid() || snapshot.sourceImage.isNull() || snapshotIds.contains(key) ||
            page == nullptr || snapshot.regions != page->regions()) {
            return makeFailedFuture<ExportResponse>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_page_snapshot", "Export page snapshot is invalid"));
        }
        snapshotIds.insert(key);
        if (request.scope == ExportScope::AllPages || selectedIds.contains(key)) {
            portRequest.pages.append(snapshot);
        }
    }
    if (request.scope == ExportScope::AllPages &&
        snapshotIds.size() != static_cast<qsizetype>(m_project->pages().size())) {
        return makeFailedFuture<ExportResponse>(makeApplicationError(
            ErrorCategory::Input, "input.missing_page_snapshot", "A project page snapshot is missing"));
    }
    if (portRequest.pages.size() !=
        (request.scope == ExportScope::AllPages ? request.pageSnapshots.size() : selectedIds.size())) {
        return makeFailedFuture<ExportResponse>(makeApplicationError(
            ErrorCategory::Input, "input.missing_page_snapshot", "A selected page snapshot is missing"));
    }
    return m_exporter->exportPages(portRequest, context);
}

} // namespace manga::application
