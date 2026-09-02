#include "KoharuLayoutPreprocessor.h"

#include <QImage>

#include <algorithm>
#include <cmath>

namespace manga::services {

KoharuPreprocessedImage
KoharuLayoutPreprocessor::process(const QImage& sourceImage, int targetWidth, int targetHeight) {
    KoharuPreprocessedImage result;
    result.originalWidth = sourceImage.width();
    result.originalHeight = sourceImage.height();
    result.targetWidth = targetWidth;
    result.targetHeight = targetHeight;

    const size_t planeSize = static_cast<size_t>(targetWidth) * targetHeight;
    result.tensorData.resize(3 * planeSize);

    if (result.originalWidth <= 0 || result.originalHeight <= 0) {
        std::fill(result.tensorData.begin(), result.tensorData.end(), 0.0f);
        return result;
    }

    // Bilinear stretch to the graph input, as in the RF-DETR Albumentations training pipeline. The page
    // is never padded or aspect preserved, so a per-axis scale alone restores coordinates.
    QImage rgbSource = sourceImage.format() == QImage::Format_RGB888
                           ? sourceImage
                           : sourceImage.convertToFormat(QImage::Format_RGB888);
    QImage scaled = rgbSource.scaled(targetWidth, targetHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    float* rChannel = result.tensorData.data();
    float* gChannel = result.tensorData.data() + planeSize;
    float* bChannel = result.tensorData.data() + 2 * planeSize;

    // ImageNet channel means and standard deviations. The detector was trained with these, so
    // normalizing differently shifts every confidence threshold downstream.
    constexpr float meanR = 0.485f;
    constexpr float meanG = 0.456f;
    constexpr float meanB = 0.406f;
    constexpr float stdR = 0.229f;
    constexpr float stdG = 0.224f;
    constexpr float stdB = 0.225f;

    for (int y = 0; y < targetHeight; ++y) {
        const uchar* line = scaled.constScanLine(y);
        const size_t rowOffset = static_cast<size_t>(y) * targetWidth;
        for (int x = 0; x < targetWidth; ++x) {
            const size_t index = rowOffset + x;
            const float r = line[x * 3 + 0] / 255.0f;
            const float g = line[x * 3 + 1] / 255.0f;
            const float b = line[x * 3 + 2] / 255.0f;

            rChannel[index] = (r - meanR) / stdR;
            gChannel[index] = (g - meanG) / stdG;
            bChannel[index] = (b - meanB) / stdB;
        }
    }

    return result;
}

} // namespace manga::services
