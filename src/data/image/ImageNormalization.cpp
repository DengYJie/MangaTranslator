#include "data/image/ImageNormalization.h"

#include <QColorSpace>
#include <QFile>
#include <QImageReader>

#include <array>
#include <cstring>

namespace manga::data {
namespace {

constexpr int kMaxSidePixels = 16384;
constexpr qint64 kMaxImageBytes = 256LL * 1024LL * 1024LL;
constexpr qsizetype kScanWindow = 256 * 1024;

application::ApplicationError makeTooLargeError() {
    return application::makeApplicationError(application::ErrorCategory::Input,
                                             QStringLiteral("input.image_too_large"),
                                             QStringLiteral("The image exceeds the supported size limit"));
}

application::ApplicationError makeDecodeFailedError() {
    return application::makeApplicationError(application::ErrorCategory::Input,
                                             QStringLiteral("import.decode_failed"),
                                             QStringLiteral("The image could not be decoded"));
}

application::ApplicationError makeCancelledError() {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.decode"),
                                             QStringLiteral("Image decoding was cancelled"));
}

struct JpegScan {
    bool hasIccProfile{false};
    bool isCmyk{false};
};

// Scans JPEG segment headers for an ICC_PROFILE APP2 marker and a 4-component frame
// (CMYK/YCCK). Parsing stops at the start of scan data; no entropy data is touched.
JpegScan scanJpegSegments(const QByteArray& data) {
    JpegScan scan;
    qsizetype offset = 2; // SOI
    while (offset + 4 <= data.size()) {
        if (data[offset] != '\xFF') {
            ++offset;
            continue;
        }
        while (offset < data.size() && data[offset] == '\xFF') {
            ++offset;
        }
        if (offset >= data.size()) {
            break;
        }
        const uchar marker = static_cast<uchar>(data[offset]);
        ++offset;
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue; // standalone markers carry no length
        }
        if (marker == 0xD9 || marker == 0xDA) {
            break; // EOI or start of scan
        }
        if (offset + 2 > data.size()) {
            break;
        }
        const qsizetype segmentLength = (static_cast<uchar>(data[offset]) << 8) | static_cast<uchar>(data[offset + 1]);
        if (segmentLength < 2 || offset + segmentLength > data.size()) {
            break;
        }
        const QByteArray segment = data.mid(offset + 2, segmentLength - 2);
        if (marker == 0xE2 && segment.startsWith("ICC_PROFILE\x00")) {
            scan.hasIccProfile = true;
        }
        if (marker == 0xEE && segment.size() >= 12 && segment.startsWith("Adobe")) {
            const uchar transform = static_cast<uchar>(segment[segment.size() - 1]);
            scan.isCmyk = scan.isCmyk || transform != 0;
        }
        const bool isSof = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isSof && segment.size() >= 6) {
            const uchar componentCount = static_cast<uchar>(segment[5]);
            scan.isCmyk = scan.isCmyk || componentCount == 4;
        }
        offset += segmentLength;
    }
    return scan;
}

struct PngScan {
    bool hasIccProfile{false};
};

// Walks PNG chunk headers before the first IDAT to find an iCCP profile.
PngScan scanPngChunks(const QByteArray& data) {
    PngScan scan;
    qsizetype offset = 8; // signature
    while (offset + 8 <= data.size()) {
        const qsizetype chunkLength =
            (static_cast<uchar>(data[offset]) << 24) | (static_cast<uchar>(data[offset + 1]) << 16) |
            (static_cast<uchar>(data[offset + 2]) << 8) | static_cast<uchar>(data[offset + 3]);
        const QByteArray type = data.mid(offset + 4, 4);
        if (chunkLength < 0 || offset + 12 + chunkLength > data.size()) {
            break;
        }
        if (type == "iCCP") {
            scan.hasIccProfile = true;
        }
        if (type == "IDAT") {
            break;
        }
        offset += 12 + chunkLength;
    }
    return scan;
}

struct WebpScan {
    bool hasIccProfile{false};
    bool isAnimated{false};
};

// Walks RIFF chunk headers to find an ICCP profile or an animation marker.
WebpScan scanWebpChunks(const QByteArray& data) {
    WebpScan scan;
    qsizetype offset = 12; // RIFF header + WEBP tag
    while (offset + 8 <= data.size()) {
        const QByteArray type = data.mid(offset, 4);
        const qsizetype chunkLength =
            static_cast<uchar>(data[offset + 4]) | (static_cast<uchar>(data[offset + 5]) << 8) |
            (static_cast<uchar>(data[offset + 6]) << 16) | (static_cast<uchar>(data[offset + 7]) << 24);
        if (chunkLength < 0 || offset + 8 + chunkLength > data.size()) {
            break;
        }
        if (type == "ICCP") {
            scan.hasIccProfile = true;
        }
        if (type == "ANIM" || type == "ANMF") {
            scan.isAnimated = true;
        }
        if (type == "VP8X" && chunkLength >= 1) {
            scan.isAnimated = scan.isAnimated || (static_cast<uchar>(data[offset + 8]) & 0x02) != 0;
        }
        offset += 8 + chunkLength + (chunkLength % 2);
    }
    return scan;
}

struct FormatScan {
    bool hasIccProfile{false};
    bool isAnimated{false};
    bool isCmyk{false};
};

FormatScan scanImageContent(ImageFileFormat format, const QByteArray& data) {
    FormatScan scan;
    switch (format) {
        case ImageFileFormat::Png: {
            const PngScan png = scanPngChunks(data);
            scan.hasIccProfile = png.hasIccProfile;
            break;
        }
        case ImageFileFormat::Jpeg: {
            const JpegScan jpeg = scanJpegSegments(data);
            scan.hasIccProfile = jpeg.hasIccProfile;
            scan.isCmyk = jpeg.isCmyk;
            break;
        }
        case ImageFileFormat::Webp: {
            const WebpScan webp = scanWebpChunks(data);
            scan.hasIccProfile = webp.hasIccProfile;
            scan.isAnimated = webp.isAnimated;
            break;
        }
        case ImageFileFormat::Unknown:
            break;
    }
    return scan;
}

QByteArray readScanWindow(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.read(kScanWindow);
}

std::expected<NormalizedImage, application::ApplicationError>
normalizeDecodedImage(QImage image, ImageFileFormat format, const FormatScan& scan, std::stop_token stopToken) {
    if (image.isNull()) {
        return std::unexpected(makeDecodeFailedError());
    }
    NormalizedImage result;
    if (scan.isCmyk) {
        result.warnings.append(
            {QStringLiteral("import.cmyk_converted"), QStringLiteral("A CMYK image was converted to sRGB")});
    }
    if (scan.isAnimated && format == ImageFileFormat::Webp) {
        result.warnings.append({QStringLiteral("import.animated_webp_first_frame"),
                                QStringLiteral("Only the first frame of an animated image was used")});
    }

    const QColorSpace sRgb(QColorSpace::SRgb);
    if (scan.hasIccProfile) {
        const QColorSpace profile = image.colorSpace();
        if (profile.isValid() && profile != sRgb) {
            image.convertToColorSpace(sRgb);
        } else {
            result.warnings.append({QStringLiteral("import.invalid_color_profile_assumed_srgb"),
                                    QStringLiteral("An unusable color profile was ignored and sRGB was assumed")});
        }
    }

    if (stopToken.stop_requested()) {
        return std::unexpected(makeCancelledError());
    }

    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setColorSpace(sRgb);

    if (!imageDimensionsWithinLimits(image.width(), image.height())) {
        return std::unexpected(makeTooLargeError());
    }
    result.image = image;
    return result;
}

} // namespace

ImageFileFormat detectImageFileFormat(const QByteArray& head) {
    const auto* data = reinterpret_cast<const uchar*>(head.constData());
    const qsizetype size = head.size();
    auto matches = [data, size](const uchar* bytes, size_t count, qsizetype offset) {
        return size >= offset + static_cast<qsizetype>(count) && std::memcmp(data + offset, bytes, count) == 0;
    };
    static constexpr uchar kPngSignature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    static constexpr uchar kJpegSoi[] = {0xFF, 0xD8, 0xFF};
    static constexpr uchar kRiff[] = {0x52, 0x49, 0x46, 0x46};
    static constexpr uchar kWebp[] = {0x57, 0x45, 0x42, 0x50};
    if (matches(kPngSignature, sizeof kPngSignature, 0)) {
        return ImageFileFormat::Png;
    }
    if (matches(kJpegSoi, sizeof kJpegSoi, 0)) {
        return ImageFileFormat::Jpeg;
    }
    if (matches(kRiff, sizeof kRiff, 0) && matches(kWebp, sizeof kWebp, 8)) {
        return ImageFileFormat::Webp;
    }
    return ImageFileFormat::Unknown;
}

QString canonicalAssetSuffix(ImageFileFormat format) {
    switch (format) {
        case ImageFileFormat::Png:
            return QStringLiteral(".png");
        case ImageFileFormat::Jpeg:
            return QStringLiteral(".jpg");
        case ImageFileFormat::Webp:
            return QStringLiteral(".webp");
        case ImageFileFormat::Unknown:
            return {};
    }
    return {};
}

bool imageDimensionsWithinLimits(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width > kMaxSidePixels || height > kMaxSidePixels) {
        return false;
    }
    return static_cast<qint64>(width) * static_cast<qint64>(height) * 4 <= kMaxImageBytes;
}

std::expected<NormalizedImage, application::ApplicationError> loadNormalizedImage(const QString& filePath,
                                                                                  std::stop_token stopToken) {
    if (stopToken.stop_requested()) {
        return std::unexpected(makeCancelledError());
    }

    const QByteArray head = readScanWindow(filePath);
    const ImageFileFormat format = detectImageFileFormat(head);
    if (format == ImageFileFormat::Unknown) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("import.unsupported_format"),
                                              QStringLiteral("The file is not a supported image format")));
    }
    const FormatScan scan = scanImageContent(format, head);

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QSize headerSize = reader.size();
    if (headerSize.isValid() && !imageDimensionsWithinLimits(headerSize.width(), headerSize.height())) {
        return std::unexpected(makeTooLargeError());
    }

    QImage image = reader.read();
    if (image.isNull()) {
        if (stopToken.stop_requested()) {
            return std::unexpected(makeCancelledError());
        }
        auto error = makeDecodeFailedError();
        error.diagnostic = reader.errorString();
        return std::unexpected(error);
    }
    return normalizeDecodedImage(image, format, scan, stopToken);
}

} // namespace manga::data
