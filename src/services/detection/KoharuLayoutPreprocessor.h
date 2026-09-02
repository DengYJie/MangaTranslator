#pragma once

#include <QImage>

#include <vector>

namespace manga::services {

/// Geometry and tensor buffer produced by the stretch preprocessor.
struct KoharuPreprocessedImage {
    std::vector<float> tensorData; // NCHW [1, 3, targetHeight, targetWidth], ImageNet normalized
    int originalWidth{0};
    int originalHeight{0};
    int targetWidth{1152};
    int targetHeight{1152};
};

class KoharuLayoutPreprocessor {
public:
    /// Stretches a full page image into a float32 RGB NCHW tensor of the target size. The aspect ratio is
    /// discarded and nothing is padded, so geometry restores with a per-axis scale of the source size
    /// against the target size.
    [[nodiscard]] static KoharuPreprocessedImage
    process(const QImage& sourceImage, int targetWidth = 1152, int targetHeight = 1152);
};

} // namespace manga::services
