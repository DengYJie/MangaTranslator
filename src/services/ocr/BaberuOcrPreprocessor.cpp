#include "BaberuOcrPreprocessor.h"

#include <algorithm>
#include <cmath>

namespace manga::services {

application::Result<std::vector<float>> BaberuOcrPreprocessor::preprocess(const application::OcrRequest& request,
                                                                          const OcrModelManifest& manifest) {
    if (request.image.isNull()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.invalid_ocr_region"),
                                                                 QStringLiteral("Image is null")));
    }

    if (manifest.input.sourceConversion != QStringLiteral("RGB") ||
        (manifest.input.resizeMode != QStringLiteral("stretch") &&
         manifest.input.resizeMode != QStringLiteral("resize-only")) ||
        manifest.input.resample != QStringLiteral("bicubic")) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("input.invalid_ocr_region"),
                                              QStringLiteral("Unsupported preprocessing parameters")));
    }

    const double rectLeft = request.rect.x;
    const double rectTop = request.rect.y;
    const double rectRight = request.rect.x + request.rect.width;
    const double rectBottom = request.rect.y + request.rect.height;

    if (std::isnan(rectLeft) || std::isnan(rectTop) || std::isnan(rectRight) || std::isnan(rectBottom)) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.invalid_ocr_region"),
                                                                 QStringLiteral("Invalid region geometry (NaN)")));
    }

    // Expand to integer pixels
    int left = static_cast<int>(std::floor(rectLeft));
    int top = static_cast<int>(std::floor(rectTop));
    int right = static_cast<int>(std::ceil(rectRight));
    int bottom = static_cast<int>(std::ceil(rectBottom));

    // Clamp to image bounds
    left = std::clamp(left, 0, request.image.width());
    top = std::clamp(top, 0, request.image.height());
    right = std::clamp(right, 0, request.image.width());
    bottom = std::clamp(bottom, 0, request.image.height());

    int pixelWidth = right - left;
    int pixelHeight = bottom - top;

    if (pixelWidth < 2 || pixelHeight < 2) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.invalid_ocr_region"),
                                                                 QStringLiteral("Intersection too small or empty")));
    }

    QImage crop = request.image.copy(left, top, pixelWidth, pixelHeight);
    QImage rgb = crop.convertToFormat(QImage::Format_RGB888);

    const int dstW = manifest.input.width;
    const int dstH = manifest.input.height;

    QImage resized = bicubicResize(rgb, dstW, dstH);

    const int tensorSize = 1 * 3 * dstW * dstH;
    std::vector<float> tensor(tensorSize);

    const float scale = manifest.normalization.scale; // typically 1/255.0
    const auto& mean = manifest.normalization.mean;
    const auto& std = manifest.normalization.std;

    for (int y = 0; y < dstH; ++y) {
        const uchar* scanLine = resized.constScanLine(y);
        for (int x = 0; x < dstW; ++x) {
            for (int c = 0; c < 3; ++c) {
                float pixel = scanLine[x * 3 + c] * scale;
                float norm = (pixel - mean[c]) / std[c];
                // NCHW format
                tensor[c * (dstH * dstW) + y * dstW + x] = norm;
            }
        }
    }

    return tensor;
}

// Bicubic kernel with keys a = -0.5, matching PIL Image.BICUBIC, so the golden tensor test holds at 1e-6.
float BaberuOcrPreprocessor::bicubicWeight(float t) noexcept {
    const float a = -0.5f;
    t = std::abs(t);
    if (t < 1.0f) {
        return (a + 2.0f) * t * t * t - (a + 3.0f) * t * t + 1.0f;
    }
    if (t < 2.0f) {
        return a * t * t * t - 5.0f * a * t * t + 8.0f * a * t - 4.0f * a;
    }
    return 0.0f;
}

QImage BaberuOcrPreprocessor::bicubicResize(const QImage& src, int dstW, int dstH) {
    QImage dst(dstW, dstH, QImage::Format_RGB888);
    if (src.isNull() || dstW <= 0 || dstH <= 0)
        return dst;

    const int srcW = src.width();
    const int srcH = src.height();

    for (int dy = 0; dy < dstH; ++dy) {
        uchar* dstLine = dst.scanLine(dy);
        // Maps the destination pixel center back into source space so the four taps stay symmetric about
        // the sampled point. A plain ratio scale anchors them at the pixel corner instead and shifts
        // every weight by half a pixel.
        float sy = (dy + 0.5f) * srcH / dstH - 0.5f;
        int iy = static_cast<int>(std::floor(sy));

        float wy[4];
        for (int k = 0; k < 4; ++k) {
            wy[k] = bicubicWeight(sy - (iy - 1 + k));
        }

        for (int dx = 0; dx < dstW; ++dx) {
            float sx = (dx + 0.5f) * srcW / dstW - 0.5f;
            int ix = static_cast<int>(std::floor(sx));

            float wx[4];
            for (int k = 0; k < 4; ++k) {
                wx[k] = bicubicWeight(sx - (ix - 1 + k));
            }

            double sumR = 0.0;
            double sumG = 0.0;
            double sumB = 0.0;

            for (int ky = 0; ky < 4; ++ky) {
                int py = std::clamp(iy - 1 + ky, 0, srcH - 1);
                const uchar* srcLine = src.constScanLine(py);
                float weightY = wy[ky];

                for (int kx = 0; kx < 4; ++kx) {
                    int px = std::clamp(ix - 1 + kx, 0, srcW - 1);
                    float weight = weightY * wx[kx];

                    sumR += srcLine[px * 3 + 0] * weight;
                    sumG += srcLine[px * 3 + 1] * weight;
                    sumB += srcLine[px * 3 + 2] * weight;
                }
            }

            dstLine[dx * 3 + 0] = static_cast<uchar>(std::clamp(std::round(sumR), 0.0, 255.0));
            dstLine[dx * 3 + 1] = static_cast<uchar>(std::clamp(std::round(sumG), 0.0, 255.0));
            dstLine[dx * 3 + 2] = static_cast<uchar>(std::clamp(std::round(sumB), 0.0, 255.0));
        }
    }
    return dst;
}

} // namespace manga::services
