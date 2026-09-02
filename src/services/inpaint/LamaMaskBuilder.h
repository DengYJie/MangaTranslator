#pragma once

#include "domain/model/Geometry.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>

#include <optional>

namespace manga::services {

/// The remove-mask for one clear operation plus the page region it covers.
///
/// `mask` is `Format_Grayscale8` in coordinates local to `dirtyBounds`, same size as
/// `dirtyBounds`, 255 meaning remove. `dirtyBounds` already carries the context margin and
/// is clipped to the page.
struct LamaClearMask {
    QImage mask;
    QRect dirtyBounds;
};

/// Builds the clear mask for a text region.
///
/// Glyph pixels are derived by colour distance from the sampled background inside the text
/// rect, then dilated so antialiased edges are covered. The result is clipped to the bubble
/// `polygon` shripped inward so the bubble outline itself is never removed. When the derived
/// ink covers an implausible fraction of that area, the whole clipped rect is used instead.
/// Without a usable polygon the fallback is the rect dilated by three pixels.
///
/// `pageImage` may be any Qt format and is converted internally.
[[nodiscard]] LamaClearMask buildClearMask(const QImage& pageImage,
                                           const QRect& regionRect,
                                           const std::optional<domain::PolygonGeometry>& polygon,
                                           QSize pageSize,
                                           int contextMargin);

/// Returns the flat fill colour for a uniform bubble background, mirroring the acceptance rule
/// that samples only background pixels inside the polygon and away from its outline.
/// Returns `std::nullopt` when the sampled background varies too much to be considered flat.
[[nodiscard]] std::optional<QColor> uniformBackgroundColor(const QImage& pageImage,
                                                           const LamaClearMask& clearMask,
                                                           const std::optional<domain::PolygonGeometry>& polygon);

/// Builds a `Format_RGBA8888` sRGB patch over `bounds` whose alpha follows `mask` per pixel,
/// so nothing outside the mask is painted over the page.
[[nodiscard]] QImage makeColorPatch(const QRect& bounds, const QColor& color, const QImage& mask);

/// Same as `makeColorPatch` but the RGB comes from `source`, which must match `mask` in size.
[[nodiscard]] QImage makeInpaintedPatch(const QRect& bounds, const QImage& source, const QImage& mask);

} // namespace manga::services
