#pragma once

#include "application/dto/ImportDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/IPageImageLoader.h"
#include "domain/model/ComicProject.h"

#include <QDir>
#include <QSet>

#include <algorithm>
#include <utility>

namespace manga::application {

struct ApplyImportResultRequest {
    domain::ProjectId projectId;
    ImportImagesResult result;
    quint64 generation{0};
    quint64 currentGeneration{0};
};

namespace detail {

[[nodiscard]] inline int compareNaturalFilePaths(QString lhs, QString rhs) {
    lhs = lhs.normalized(QString::NormalizationForm_KC).toCaseFolded();
    rhs = rhs.normalized(QString::NormalizationForm_KC).toCaseFolded();

    qsizetype lhsIndex = 0;
    qsizetype rhsIndex = 0;
    while (lhsIndex < lhs.size() && rhsIndex < rhs.size()) {
        if (lhs[lhsIndex].isDigit() && rhs[rhsIndex].isDigit()) {
            const auto lhsRunStart = lhsIndex;
            const auto rhsRunStart = rhsIndex;
            while (lhsIndex < lhs.size() && lhs[lhsIndex].isDigit()) {
                ++lhsIndex;
            }
            while (rhsIndex < rhs.size() && rhs[rhsIndex].isDigit()) {
                ++rhsIndex;
            }

            auto lhsSignificant = lhsRunStart;
            auto rhsSignificant = rhsRunStart;
            while (lhsSignificant + 1 < lhsIndex && lhs[lhsSignificant].digitValue() == 0) {
                ++lhsSignificant;
            }
            while (rhsSignificant + 1 < rhsIndex && rhs[rhsSignificant].digitValue() == 0) {
                ++rhsSignificant;
            }

            const auto lhsDigits = lhsIndex - lhsSignificant;
            const auto rhsDigits = rhsIndex - rhsSignificant;
            if (lhsDigits != rhsDigits) {
                return lhsDigits < rhsDigits ? -1 : 1;
            }
            for (qsizetype offset = 0; offset < lhsDigits; ++offset) {
                const auto lhsDigit = lhs[lhsSignificant + offset].digitValue();
                const auto rhsDigit = rhs[rhsSignificant + offset].digitValue();
                if (lhsDigit != rhsDigit) {
                    return lhsDigit < rhsDigit ? -1 : 1;
                }
            }

            const auto lhsRunLength = lhsIndex - lhsRunStart;
            const auto rhsRunLength = rhsIndex - rhsRunStart;
            if (lhsRunLength != rhsRunLength) {
                return lhsRunLength < rhsRunLength ? -1 : 1;
            }
            continue;
        }

        if (lhs[lhsIndex] != rhs[rhsIndex]) {
            return lhs[lhsIndex] < rhs[rhsIndex] ? -1 : 1;
        }
        ++lhsIndex;
        ++rhsIndex;
    }

    if (lhsIndex == lhs.size() && rhsIndex == rhs.size()) {
        return 0;
    }
    return lhsIndex == lhs.size() ? -1 : 1;
}

} // namespace detail

class ImportImagesUseCase {
public:
    explicit ImportImagesUseCase(IComicImporter* importer) : m_importer(importer) {}

    [[nodiscard]] QFuture<Result<ImportImagesResult>> execute(const ImportImagesRequest& request,
                                                              const OperationContext& context) {
        if (m_importer == nullptr) {
            return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_importer", "Image importer is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<ImportImagesResult>(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.before_start", "Import was cancelled"));
        }
        if (!request.projectId.isValid() || !QDir::isAbsolutePath(request.projectRoot.trimmed())) {
            return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_import_request", "Import request is invalid"));
        }
        if (request.filePaths.isEmpty()) {
            return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                ErrorCategory::Input, "input.empty_import", "Select at least one image to import"));
        }
        QSet<QString> paths;
        ImportImagesRequest sortedRequest = request;
        sortedRequest.projectRoot = sortedRequest.projectRoot.trimmed();
        for (auto& path : sortedRequest.filePaths) {
            path = path.trimmed();
            const auto comparisonKey = QDir::cleanPath(path).normalized(QString::NormalizationForm_KC).toCaseFolded();
            if (path.isEmpty()) {
                return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                    ErrorCategory::Input, "input.invalid_import_request", "Import paths must be nonempty"));
            }
            if (paths.contains(comparisonKey)) {
                return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                    ErrorCategory::Input, "input.duplicate_import_path", "Import paths must be unique"));
            }
            paths.insert(comparisonKey);
        }

        std::stable_sort(
            sortedRequest.filePaths.begin(), sortedRequest.filePaths.end(), [](const QString& lhs, const QString& rhs) {
                const auto normalizedLhs = lhs.normalized(QString::NormalizationForm_KC);
                const auto normalizedRhs = rhs.normalized(QString::NormalizationForm_KC);
                const auto comparison = detail::compareNaturalFilePaths(normalizedLhs, normalizedRhs);
                return comparison != 0 ? comparison < 0 : normalizedLhs < normalizedRhs;
            });

        return m_importer->importImages(sortedRequest, context);
    }

private:
    IComicImporter* m_importer;
};

class LoadPageImageUseCase {
public:
    explicit LoadPageImageUseCase(IPageImageLoader* loader) : m_loader(loader) {}

    [[nodiscard]] QFuture<Result<QImage>> execute(const LoadPageImageRequest& request,
                                                  const OperationContext& context) {
        if (m_loader == nullptr) {
            return makeFailedFuture<QImage>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_image_loader", "Page image loader is unavailable"));
        }
        if (context.stopToken.stop_requested()) {
            return makeFailedFuture<QImage>(makeApplicationError(
                ErrorCategory::Cancelled, "cancelled.before_start", "Image loading was cancelled"));
        }
        if (!request.projectId.isValid() || !QDir::isAbsolutePath(request.projectRoot.trimmed()) ||
            !request.pageId.isValid() || request.assetPath.trimmed().isEmpty() || !request.expectedSize.isValid()) {
            return makeFailedFuture<QImage>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_image_request", "Page image request is invalid"));
        }
        auto normalizedRequest = request;
        normalizedRequest.projectRoot = normalizedRequest.projectRoot.trimmed();
        normalizedRequest.assetPath = normalizedRequest.assetPath.trimmed();
        return m_loader->loadPageImage(std::move(normalizedRequest), context);
    }

private:
    IPageImageLoader* m_loader;
};

class ApplyImportResultUseCase {
public:
    explicit ApplyImportResultUseCase(domain::ComicProject* project) : m_project(project) {}

    [[nodiscard]] Result<QVector<domain::PageId>> execute(const ApplyImportResultRequest& request) {
        if (m_project == nullptr) {
            return std::unexpected(
                makeApplicationError(ErrorCategory::Internal, "internal.null_project", "Project is unavailable"));
        }
        if (m_project->id() != request.projectId) {
            return std::unexpected(makeApplicationError(ErrorCategory::Project, "project.mismatch", "Project changed"));
        }
        if (request.generation != request.currentGeneration) {
            return std::unexpected(
                makeApplicationError(ErrorCategory::Cancelled, "cancelled.stale_generation", "Import result is stale"));
        }
        if (!request.result.importedPages.isEmpty() && !request.result.batchId.isValid()) {
            return std::unexpected(makeApplicationError(
                ErrorCategory::Protocol, "protocol.invalid_import_result", "Imported asset batch is invalid"));
        }

        const auto snapshot = *m_project;
        QVector<domain::PageId> pageIds;
        pageIds.reserve(request.result.importedPages.size());
        for (const auto& imported : request.result.importedPages) {
            if (!imported.page.isValid() || !m_project->addPage(imported.page)) {
                *m_project = snapshot;
                return std::unexpected(makeApplicationError(
                    ErrorCategory::Protocol, "protocol.invalid_import_result", "Imported page is invalid"));
            }
            pageIds.append(imported.page.id());
        }
        return pageIds;
    }

private:
    domain::ComicProject* m_project;
};

class DiscardImportedAssetsUseCase {
public:
    explicit DiscardImportedAssetsUseCase(IComicImporter* importer) : m_importer(importer) {}

    [[nodiscard]] QFuture<Result<void>> execute(const DiscardImportedAssetsRequest& request,
                                                const OperationContext& context) {
        if (m_importer == nullptr) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_importer", "Image importer is unavailable"));
        }
        if (!request.projectId.isValid() || !QDir::isAbsolutePath(request.projectRoot.trimmed()) ||
            !request.batchId.isValid() || request.assetPaths.isEmpty()) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_import_cleanup", "Import cleanup request is invalid"));
        }

        auto normalizedRequest = request;
        normalizedRequest.projectRoot = normalizedRequest.projectRoot.trimmed();
        QSet<QString> assetPaths;
        for (auto& path : normalizedRequest.assetPaths) {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
            const auto comparisonKey = path.normalized(QString::NormalizationForm_KC).toCaseFolded();
            if (path.isEmpty() || QDir::isAbsolutePath(path) || path == ".." || path.startsWith("../") ||
                !path.startsWith("assets/original/") || assetPaths.contains(comparisonKey)) {
                return makeFailedFuture<void>(makeApplicationError(
                    ErrorCategory::Input, "input.invalid_import_cleanup", "Import cleanup request is invalid"));
            }
            assetPaths.insert(comparisonKey);
        }
        return m_importer->discardImportedAssets(std::move(normalizedRequest), context);
    }

private:
    IComicImporter* m_importer;
};

class AcknowledgeImportedAssetsUseCase {
public:
    explicit AcknowledgeImportedAssetsUseCase(IComicImporter* importer) : m_importer(importer) {}

    [[nodiscard]] QFuture<Result<void>> execute(const AcknowledgeImportedAssetsRequest& request,
                                                const OperationContext& context) {
        if (m_importer == nullptr) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Internal, "internal.null_importer", "Image importer is unavailable"));
        }
        if (!request.projectId.isValid() || !QDir::isAbsolutePath(request.projectRoot.trimmed()) ||
            !request.batchId.isValid()) {
            return makeFailedFuture<void>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_import_cleanup", "Import acknowledgement is invalid"));
        }
        auto normalizedRequest = request;
        normalizedRequest.projectRoot = normalizedRequest.projectRoot.trimmed();
        return m_importer->acknowledgeImportedAssets(std::move(normalizedRequest), context);
    }

private:
    IComicImporter* m_importer;
};

} // namespace manga::application
