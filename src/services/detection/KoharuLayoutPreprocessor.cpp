#include "KoharuLayoutPreprocessor.h"

#include <QPainter>

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

    if (result.originalWidth <= 0 || result.originalHeight <= 0) {
        result.tensorData.assign(static_cast<size_t>(3) * targetWidth * targetHeight, 0.0f);
        return result;
    }

    const double scaleX = static_cast<double>(targetWidth) / result.originalWidth;
    const double scaleY = static_cast<double>(targetHeight) / result.originalHeight;
    result.scale = std::min(scaleX, scaleY);

    result.scaledWidth = std::clamp(static_cast<int>(std::round(result.originalWidth * result.scale)), 1, targetWidth);
    result.scaledHeight =
        std::clamp(static_cast<int>(std::round(result.originalHeight * result.scale)), 1, targetHeight);

    const int padX = (targetWidth - result.scaledWidth) / 2;
    const int padY = (targetHeight - result.scaledHeight) / 2;
    result.padX = static_cast<double>(padX);
    result.padY = static_cast<double>(padY);

    // Create 1152x1152 RGB working image with 0.0 padding
    QImage canvas(targetWidth, targetHeight, QImage::Format_RGB888);
    canvas.fill(Qt::black);

    QImage rgbSource = sourceImage.format() == QImage::Format_RGB888
                           ? sourceImage
                           : sourceImage.convertToFormat(QImage::Format_RGB888);
    QImage scaled =
        rgbSource.scaled(result.scaledWidth, result.scaledHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPainter painter(&canvas);
    painter.drawImage(padX, padY, scaled);
    painter.end();

    const size_t planeSize = static_cast<size_t>(targetWidth) * targetHeight;
    result.tensorData.resize(3 * planeSize);

    float* rChannel = result.tensorData.data();
    float* gChannel = result.tensorData.data() + planeSize;
    float* bChannel = result.tensorData.data() + 2 * planeSize;

    for (int y = 0; y < targetHeight; ++y) {
        const uchar* line = canvas.constScanLine(y);
        const size_t rowOffset = static_cast<size_t>(y) * targetWidth;
        for (int x = 0; x < targetWidth; ++x) {
            const size_t index = rowOffset + x;
            rChannel[index] = line[x * 3 + 0] / 255.0f;
            gChannel[index] = line[x * 3 + 1] / 255.0f;
            bChannel[index] = line[x * 3 + 2] / 255.0f;
        }
    }

    return result;
}

} // namespace manga::services
