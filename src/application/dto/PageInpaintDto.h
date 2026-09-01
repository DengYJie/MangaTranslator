#pragma once

#include "domain/model/Uuid.h"

#include <QImage>
#include <QRect>

namespace manga::application {

struct PageInpaintRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    /// The complete composited page image in source-pixel coordinates.
    QImage sourceImage;
    /// An 8-bit grayscale mask in source-image coordinates (non-zero means remove).
    QImage mask;
    /// Tight source-image bounds of the mask, clipped to sourceImage.
    QRect dirtyBounds;
};

struct PageInpaintResponse {
    domain::ProjectId projectId;
    domain::PageId pageId;
    /// Complete page result, always the same dimensions as the request source image.
    QImage image;
    /// Tight source-image bounds changed by the inpainting operation.
    QRect dirtyBounds;
};

} // namespace manga::application
