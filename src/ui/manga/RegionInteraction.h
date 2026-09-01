#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/Uuid.h"

#include <QMetaType>
#include <QPointF>
#include <QRectF>

namespace manga::ui::canvas {

namespace domain = ::manga::domain;

/// Canvas interaction tool state.
enum class Tool {
    Select,
    Pan,
    CreateRegion,
    Remove
};

/// Selection behavior when clicking a region.
enum class SelectionIntent {
    Replace,
    Toggle,
    Range
};

/// Geometry edit classification for committed intents.
enum class EditKind {
    Move,
    Resize
};

/// Eight directional handle positions around a rectangle.
enum class HandlePosition {
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West
};

/// Renderable projection of one region on the canvas. Items hold this data only;
/// they never query Domain or ViewModel directly.
struct RegionDisplay {
    domain::RegionId id;
    domain::RectGeometry geometry;
    bool selected{false};

    bool operator==(const RegionDisplay&) const = default;
};

/// Minimum region dimension in source pixels for creation and resize clamping.
inline constexpr double kMinRegionDimension = 8.0;

/// Visual handle size in device pixels (cosmetic).
inline constexpr double kHandleVisualSizeDevicePx = 8.0;

/// Hit area for handles in device pixels. Must be at least 16 to meet touch/a11y targets.
inline constexpr double kHandleHitSizeDevicePx = 16.0;

/// Drag distance threshold in device pixels before a press becomes a drag gesture.
inline constexpr double kDragThresholdDevicePx = 4.0;

/// Converts source-pixel scene coordinates to view coordinates at a given zoom level.
[[nodiscard]] QPointF sourceToView(QPointF source, double zoom) noexcept;

/// Converts view coordinates back to source-pixel scene coordinates.
[[nodiscard]] QPointF viewToSource(QPointF view, double zoom) noexcept;

/// Converts view coordinates to device pixel coordinates using the device pixel ratio.
[[nodiscard]] QPointF viewToDevice(QPointF view, double devicePixelRatio) noexcept;

/// Converts device pixel coordinates to view coordinates.
[[nodiscard]] QPointF deviceToView(QPointF device, double devicePixelRatio) noexcept;

/// Returns the hit rectangle in device pixels for a handle at the given position
/// on a region with the specified scene-space geometry, zoom, and device pixel ratio.
[[nodiscard]] QRectF
handleHitRect(HandlePosition pos, const domain::RectGeometry& geometry, double zoom, double devicePixelRatio) noexcept;

/// Tests whether a device-pixel point hits any handle of the given region geometry.
/// Returns the handle position if hit, or std::nullopt otherwise.
[[nodiscard]] std::optional<HandlePosition> hitTestHandle(const QPointF& devicePoint,
                                                          const domain::RectGeometry& geometry,
                                                          double zoom,
                                                          double devicePixelRatio) noexcept;

/// Computes the new geometry after moving by a source-pixel delta, clamped to page bounds.
[[nodiscard]] domain::RectGeometry
moveRegion(const domain::RectGeometry& original, QPointF deltaSourcePx, const domain::ImageSize& pageBounds) noexcept;

/// Computes the new geometry after resizing from a specific handle by a source-pixel delta,
/// enforcing minimum dimensions and page boundary clamping.
/// Returns std::nullopt if the result would fall below the minimum size.
[[nodiscard]] std::optional<domain::RectGeometry> resizeRegion(const domain::RectGeometry& original,
                                                               HandlePosition handle,
                                                               QPointF deltaSourcePx,
                                                               const domain::ImageSize& pageBounds) noexcept;

/// Tests whether a device-pixel drag distance exceeds the drag threshold.
[[nodiscard]] bool exceedsDragThreshold(QPointF deviceDelta) noexcept;

} // namespace manga::ui::canvas

Q_DECLARE_METATYPE(manga::ui::canvas::SelectionIntent)
Q_DECLARE_METATYPE(manga::ui::canvas::EditKind)
