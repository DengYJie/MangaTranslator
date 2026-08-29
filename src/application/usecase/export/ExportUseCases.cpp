#include "ExportUseCases.h"

#include "application/operation/AsyncResult.h"

#include <QColorSpace>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace manga::application {
namespace {

// Mirrors the exporter contract: a safe file stem with no path segments and no trailing dot.
[[nodiscard]] bool isValidOutputStem(const QString& stem) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    return pattern.match(stem).hasMatch() && !stem.endsWith(QLatin1Char('.'));
}

[[nodiscard]] bool isValidLayerColorSpace(const QImage& image) {
    if (image.isNull()) {
        return true;
    }
    const auto cs = image.colorSpace();
    return !cs.isValid() || cs == QColorSpace(QColorSpace::SRgb) || cs.primaries() == QColorSpace::Primaries::SRgb;
}

[[nodiscard]] bool isUsableLayer(const QImage& layer, const QImage& source) {
    return layer.isNull() || (layer.size() == source.size() && layer.format() == QImage::Format_RGBA8888 &&
                              isValidLayerColorSpace(layer));
}

} // namespace

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

    QHash<QString, ExportPageSnapshot> snapshotMap;
    for (auto snapshot : request.pageSnapshots) {
        const auto key = QString::fromStdString(snapshot.pageId.toString());
        const auto* page = m_project->findPage(snapshot.pageId);
        if (!snapshot.pageId.isValid() || snapshot.sourceImage.isNull() || snapshotMap.contains(key) ||
            page == nullptr) {
            return makeFailedFuture<ExportResponse>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_page_snapshot", "Export page snapshot is invalid"));
        }
        if (snapshot.outputStem.trimmed().isEmpty()) {
            snapshot.outputStem =
                QStringLiteral("page_%1_translated").arg(page->orderIndex() + 1, 4, 10, QLatin1Char('0'));
        }
        if (!isValidOutputStem(snapshot.outputStem)) {
            return makeFailedFuture<ExportResponse>(makeApplicationError(
                ErrorCategory::Input, "export.invalid_output_name", "An export output stem is invalid"));
        }
        if (!isValidLayerColorSpace(snapshot.sourceImage) ||
            !isUsableLayer(snapshot.clearLayer, snapshot.sourceImage) ||
            !isUsableLayer(snapshot.translationLayer, snapshot.sourceImage) ||
            !isUsableLayer(snapshot.manualOverrideLayer, snapshot.sourceImage)) {
            return makeFailedFuture<ExportResponse>(
                makeApplicationError(ErrorCategory::Input, "export.invalid_layer", "An export layer is invalid"));
        }
        snapshotMap.insert(key, snapshot);
    }

    if (request.scope == ExportScope::AllPages &&
        snapshotMap.size() != static_cast<qsizetype>(m_project->pages().size())) {
        return makeFailedFuture<ExportResponse>(makeApplicationError(
            ErrorCategory::Input, "input.missing_page_snapshot", "A project page snapshot is missing"));
    }

    // Preserve natural project order
    for (const auto& page : m_project->pages()) {
        const auto key = QString::fromStdString(page.id().toString());
        if (snapshotMap.contains(key)) {
            if (request.scope == ExportScope::AllPages || selectedIds.contains(key)) {
                portRequest.pages.append(snapshotMap.value(key));
            }
        }
    }

    if (portRequest.pages.size() !=
        (request.scope == ExportScope::AllPages ? request.pageSnapshots.size() : selectedIds.size())) {
        return makeFailedFuture<ExportResponse>(makeApplicationError(
            ErrorCategory::Input, "input.missing_page_snapshot", "A selected page snapshot is missing"));
    }

    return m_exporter->exportPages(portRequest, context);
}

} // namespace manga::application
