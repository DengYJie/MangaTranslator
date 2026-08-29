#include "data/export/FileSystemComicExporter.h"

#include "data/DataAsync.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QLockFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <algorithm>
#include <utility>

namespace manga::data {
namespace {

constexpr int kMaxConflictRetries = 10;
constexpr int kLockTimeoutMs = 2000;
constexpr int kStaleLockTimeMs = 5000;

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

[[nodiscard]] bool isMatchingLayerGeometry(const QImage& layer, const QImage& source) {
    return layer.isNull() || (layer.size() == source.size() && layer.format() == QImage::Format_RGBA8888 &&
                              isValidLayerColorSpace(layer));
}

[[nodiscard]] application::ApplicationError makeExportError(const QString& code, const QString& message) {
    return application::makeApplicationError(application::ErrorCategory::Persistence, code, message);
}

} // namespace

QFuture<application::Result<application::ExportResponse>>
FileSystemComicExporter::exportPages(application::ExportRequest request, application::OperationContext context) {
    return runDataOperation<application::ExportResponse>([request = std::move(request), context]()
                                                             -> application::Result<application::ExportResponse> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.user_requested"),
                                                                     QStringLiteral("The export was cancelled")));
        }

        const QString cleanDir = QDir::cleanPath(request.outputDir.trimmed());
        if (cleanDir.isEmpty()) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Input,
                                                  QStringLiteral("export.invalid_request"),
                                                  QStringLiteral("The export output directory is empty or invalid")));
        }

        QDir dir(cleanDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Persistence,
                                                  QStringLiteral("export.invalid_request"),
                                                  QStringLiteral("The export output directory could not be created")));
        }

        const bool isJpeg = (request.format == application::ExportFormat::Jpeg);
        const QString extension = isJpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
        const char* formatName = isJpeg ? "JPEG" : "PNG";

        if (QImageWriter::supportedImageFormats().indexOf(formatName) < 0 &&
            QImageWriter::supportedImageFormats().indexOf(QByteArray(formatName).toLower()) < 0) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Internal,
                QStringLiteral("export.encoder_unavailable"),
                QStringLiteral("The image format encoder is not available on this system")));
        }

        application::ExportResponse response;
        const QString opId = context.operationId.isValid() ? QString::fromStdString(context.operationId.toString())
                                                           : QStringLiteral("op");

        for (const auto& page : request.pages) {
            if (context.stopToken.stop_requested()) {
                response.failures.append(
                    {page.pageId,
                     application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                       QStringLiteral("cancelled.user_requested"),
                                                       QStringLiteral("The export was cancelled"))});
                continue;
            }

            if (page.sourceImage.isNull() || page.sourceImage.format() != QImage::Format_RGBA8888 ||
                !isValidLayerColorSpace(page.sourceImage) ||
                !isMatchingLayerGeometry(page.clearLayer, page.sourceImage) ||
                !isMatchingLayerGeometry(page.translationLayer, page.sourceImage) ||
                !isMatchingLayerGeometry(page.manualOverrideLayer, page.sourceImage)) {
                response.failures.append(
                    {page.pageId,
                     application::makeApplicationError(
                         application::ErrorCategory::Input,
                         QStringLiteral("export.invalid_layer"),
                         QStringLiteral("An export layer is null, has invalid geometry, or is not sRGB"))});
                continue;
            }

            if (!isValidOutputStem(page.outputStem)) {
                response.failures.append(
                    {page.pageId,
                     application::makeApplicationError(application::ErrorCategory::Input,
                                                       QStringLiteral("export.invalid_output_name"),
                                                       QStringLiteral("The output filename stem is invalid"))});
                continue;
            }

            // 1. Composite layers: Original -> Clear -> Translation -> ManualOverride
            QImage composite(page.sourceImage.size(), QImage::Format_RGBA8888_Premultiplied);
            composite.setColorSpace(QColorSpace(QColorSpace::SRgb));
            composite.fill(Qt::transparent);
            {
                QPainter painter(&composite);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawImage(0, 0, page.sourceImage);
                if (!page.clearLayer.isNull()) {
                    painter.drawImage(0, 0, page.clearLayer);
                }
                if (!page.translationLayer.isNull()) {
                    painter.drawImage(0, 0, page.translationLayer);
                }
                if (!page.manualOverrideLayer.isNull()) {
                    painter.drawImage(0, 0, page.manualOverrideLayer);
                }
            }

            // 2. Format conversion: for JPEG, flatten over opaque white sRGB background; for PNG, straight RGBA8888.
            QImage toEncode;
            if (isJpeg) {
                toEncode = QImage(composite.size(), QImage::Format_RGB888);
                toEncode.setColorSpace(QColorSpace(QColorSpace::SRgb));
                toEncode.fill(Qt::white);
                QPainter painter(&toEncode);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                painter.drawImage(0, 0, composite);
            } else {
                toEncode = composite.convertToFormat(QImage::Format_RGBA8888);
            }

            // 3. Acquire short-lived directory lock
            QLockFile lock(dir.filePath(QStringLiteral(".mangatranslator-export.lock")));
            lock.setStaleLockTime(kStaleLockTimeMs);
            if (!lock.tryLock(kLockTimeoutMs)) {
                response.failures.append(
                    {page.pageId,
                     makeExportError(QStringLiteral("export.output_locked"),
                                     QStringLiteral("The export directory is locked by another operation"))});
                continue;
            }

            // 4. Retry loop for conflict resolution & staging commit
            bool committed = false;
            bool exhausted = false;

            for (int attempt = 0; attempt <= kMaxConflictRetries; ++attempt) {
                QString fileName;
                if (attempt == 0) {
                    fileName = page.outputStem + extension;
                } else {
                    fileName = QStringLiteral("%1_%2%3")
                                   .arg(page.outputStem)
                                   .arg(attempt, 3, 10, QLatin1Char('0'))
                                   .arg(extension);
                }
                const QString candidatePath = dir.filePath(fileName);
                if (QFileInfo::exists(candidatePath)) {
                    if (attempt == kMaxConflictRetries) {
                        exhausted = true;
                    }
                    continue;
                }

                // 5. Staging write in same directory via temporary file with operationId
                const QString tempPattern = QStringLiteral(".mangatranslator_export_%1_XXXXXX.tmp").arg(opId);
                QTemporaryFile tempFile(dir.filePath(tempPattern));
                if (!tempFile.open()) {
                    response.failures.append(
                        {page.pageId,
                         makeExportError(QStringLiteral("export.encode_failed"),
                                         QStringLiteral("Failed to create temporary export file"))});
                    break;
                }

                QImageWriter writer(&tempFile, formatName);
                if (!writer.canWrite()) {
                    tempFile.remove();
                    response.failures.append(
                        {page.pageId,
                         makeExportError(QStringLiteral("export.encoder_unavailable"),
                                         QStringLiteral("The image writer cannot write target format"))});
                    break;
                }

                if (isJpeg) {
                    writer.setQuality(std::clamp(request.jpegQuality, 1, 100));
                }
                if (!writer.write(toEncode)) {
                    const QString err = writer.errorString();
                    tempFile.remove();
                    response.failures.append(
                        {page.pageId,
                         makeExportError(QStringLiteral("export.encode_failed"),
                                         QStringLiteral("Failed to encode export image: ") + err)});
                    break;
                }
                if (!tempFile.flush()) {
                    tempFile.remove();
                    response.failures.append(
                        {page.pageId,
                         makeExportError(QStringLiteral("export.encode_failed"),
                                         QStringLiteral("Failed to flush encoded image to temporary file"))});
                    break;
                }
                tempFile.close();

                if (context.stopToken.stop_requested()) {
                    tempFile.remove();
                    response.failures.append(
                        {page.pageId,
                         application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                           QStringLiteral("cancelled.user_requested"),
                                                           QStringLiteral("The export was cancelled"))});
                    break;
                }

                // 6. Atomic rename to candidate path
                if (!tempFile.rename(candidatePath)) {
                    tempFile.remove();
                    // Race collision: candidate appeared between check and rename. Try next attempt.
                    if (attempt == kMaxConflictRetries) {
                        response.failures.append(
                            {page.pageId,
                             makeExportError(QStringLiteral("export.commit_failed"),
                                             QStringLiteral("Failed to commit temporary file to target path"))});
                    }
                    continue;
                }

                tempFile.setAutoRemove(false);
                response.exported.append({page.pageId, QDir::toNativeSeparators(candidatePath)});
                committed = true;
                break;
            }

            lock.unlock();

            if (!committed && exhausted) {
                response.failures.append(
                    {page.pageId,
                     makeExportError(QStringLiteral("export.name_exhausted"),
                                     QStringLiteral("All candidate output filenames conflict with existing files"))});
            }
        }

        return response;
    });
}

} // namespace manga::data
