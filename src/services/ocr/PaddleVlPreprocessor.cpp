#include "PaddleVlPreprocessor.h"

#include <algorithm>
#include <cmath>

namespace manga::services {

application::Result<PaddleVlPreprocessedData> PaddleVlPreprocessor::preprocess(const application::OcrRequest& request) {
    if (request.image.isNull() || request.rect.width <= 0 || request.rect.height <= 0) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("input.invalid_image"),
                                              QStringLiteral("Invalid crop region for PaddleOCR-VL")));
    }

    const QRect cropRect(std::clamp(qRound(request.rect.x), 0, request.image.width() - 1),
                         std::clamp(qRound(request.rect.y), 0, request.image.height() - 1),
                         std::clamp(qRound(request.rect.width), 1, request.image.width()),
                         std::clamp(qRound(request.rect.height), 1, request.image.height()));

    QImage cropped = request.image.copy(cropRect).convertToFormat(QImage::Format_RGB888);
    if (cropped.isNull() || cropped.width() == 0 || cropped.height() == 0) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("input.crop_empty"),
                                              QStringLiteral("Cropped image is empty for PaddleOCR-VL")));
    }

    const int w = cropped.width();
    const int h = cropped.height();
    constexpr int step = kPatchSize * kMergeSize; // 14 * 2 = 28

    // Calculate target dimensions (W', H') that maintain aspect ratio and are multiples of step
    double area = static_cast<double>(w) * h;
    double scale = 1.0;
    if (area < kMinPixels) {
        scale = std::sqrt(static_cast<double>(kMinPixels) / area);
    } else if (area > kMaxPixels) {
        scale = std::sqrt(static_cast<double>(kMaxPixels) / area);
    }

    int targetW = std::max(step, static_cast<int>(std::round(w * scale / step)) * step);
    int targetH = std::max(step, static_cast<int>(std::round(h * scale / step)) * step);

    // Adjust to fit in [kMinPixels, kMaxPixels]
    while (targetW * targetH < kMinPixels) {
        if (targetW <= targetH) {
            targetW += step;
        } else {
            targetH += step;
        }
    }
    while (targetW * targetH > kMaxPixels && targetW > step && targetH > step) {
        if (targetW >= targetH) {
            targetW -= step;
        } else {
            targetH -= step;
        }
    }

    QImage scaled = cropped.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const int patchesH = targetH / kPatchSize;
    const int patchesW = targetW / kPatchSize;
    const int numPatches = patchesH * patchesW;
    const int mergedTokens = numPatches / (kMergeSize * kMergeSize);

    PaddleVlPreprocessedData data;
    data.patchesH = patchesH;
    data.patchesW = patchesW;
    data.numPatches = numPatches;
    data.mergedTokens = mergedTokens;
    data.gridThw = {1, patchesH, patchesW};

    // Pixel values tensor shape: [1, P, 3, 14, 14]
    // Normalized as (val / 255.0f - 0.5f) / 0.5f
    data.pixelValues.resize(1 * numPatches * 3 * kPatchSize * kPatchSize);
    float* outPtr = data.pixelValues.data();

    for (int py = 0; py < patchesH; ++py) {
        for (int px = 0; px < patchesW; ++px) {
            const int startY = py * kPatchSize;
            const int startX = px * kPatchSize;

            for (int c = 0; c < 3; ++c) {
                for (int y = 0; y < kPatchSize; ++y) {
                    const uchar* line = scaled.scanLine(startY + y);
                    for (int x = 0; x < kPatchSize; ++x) {
                        float val = static_cast<float>(line[(startX + x) * 3 + c]);
                        *outPtr++ = (val / 255.0f - 0.5f) / 0.5f;
                    }
                }
            }
        }
    }

    return data;
}

} // namespace manga::services
