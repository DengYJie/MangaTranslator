#pragma once

#include "application/error/ApplicationError.h"

#include <QImage>
#include <QString>
#include <QVector>

#include <expected>
#include <stop_token>
#include <utility>

namespace manga::data {

enum class ImageFileFormat {
    Unknown,
    Png,
    Jpeg,
    Webp,
};

/// Decoded and normalized page image plus non-fatal warnings as (code, safeMessage) pairs.
struct NormalizedImage {
    QImage image;
    QVector<QPair<QString, QString>> warnings;
};

/// Detects a whitelisted format from magic bytes only; source file extensions are never trusted.
[[nodiscard]] ImageFileFormat detectImageFileFormat(const QByteArray& head);

/// Returns the canonical asset suffix ".png", ".jpg", or ".webp"; empty for unknown formats.
[[nodiscard]] QString canonicalAssetSuffix(ImageFileFormat format);

/// True when width and height fit the V0.1 decode limits (max 16384 px per side and the
/// RGBA8 memory budget).
[[nodiscard]] bool imageDimensionsWithinLimits(int width, int height);

/// Decodes an image file and normalizes it: magic-byte sniffing, header precheck, one EXIF
/// transform, ICC to sRGB, straight-alpha RGBA8. The source file is never modified.
[[nodiscard]] std::expected<NormalizedImage, application::ApplicationError>
loadNormalizedImage(const QString& filePath, std::stop_token stopToken = {});

} // namespace manga::data
