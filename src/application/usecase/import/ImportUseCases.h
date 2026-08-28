#pragma once

#include "application/dto/ImportDto.h"
#include "application/operation/AsyncResult.h"
#include "application/ports/IComicImporter.h"
#include "application/ports/IPageImageLoader.h"

#include <QDir>
#include <QSet>

#include <algorithm>

namespace manga::application {

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
        if (!request.projectId.isValid() || request.filePaths.isEmpty()) {
            return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                ErrorCategory::Input, "input.empty_import", "Select at least one image to import"));
        }
        QSet<QString> paths;
        ImportImagesRequest sortedRequest = request;
        for (auto& path : sortedRequest.filePaths) {
            path = path.trimmed();
            const auto comparisonKey = QDir::cleanPath(path).normalized(QString::NormalizationForm_KC).toCaseFolded();
            if (path.isEmpty() || paths.contains(comparisonKey)) {
                return makeFailedFuture<ImportImagesResult>(makeApplicationError(
                    ErrorCategory::Input, "input.invalid_import_path", "Import paths must be nonempty and unique"));
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
        if (!request.projectId.isValid() || !request.pageId.isValid() || request.assetPath.trimmed().isEmpty()) {
            return makeFailedFuture<QImage>(makeApplicationError(
                ErrorCategory::Input, "input.invalid_image_request", "Page image request is invalid"));
        }
        return m_loader->loadPageImage(request, context);
    }

private:
    IPageImageLoader* m_loader;
};

} // namespace manga::application
