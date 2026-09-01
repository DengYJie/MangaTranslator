#pragma once

#include <QImage>

#include <vector>

namespace manga::services {

/// Parameters and tensor buffer resulting from Letterbox preprocessing.
struct KoharuPreprocessedImage {
    std::vector<float> tensorData; // NCHW [1, 3, targetHeight, targetWidth]
    int originalWidth{0};
    int originalHeight{0};
    int targetWidth{1152};
    int targetHeight{1152};
    double scale{1.0};
    double padX{0.0};
    double padY{0.0};
    int scaledWidth{0};
    int scaledHeight{0};
};

class KoharuLayoutPreprocessor {
public:
    /// Prepares a full page image into a 1152x1152 float32 RGB NCHW tensor with Letterbox padding.
    [[nodiscard]] static KoharuPreprocessedImage
    process(const QImage& sourceImage, int targetWidth = 1152, int targetHeight = 1152);
};

} // namespace manga::services
