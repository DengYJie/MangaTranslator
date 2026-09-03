#pragma once

#include <QtGlobal>

/// Z-order constants for the manga canvas scene. Higher values paint above lower ones.
namespace manga::ui::canvas {

inline constexpr qreal kZBackground = 0.0;
inline constexpr qreal kZPageOriginal = 10.0;
inline constexpr qreal kZClearLayer = 20.0;
inline constexpr qreal kZManualOverrideLayer = 25.0;
inline constexpr qreal kZRegionNormal = 100.0;
inline constexpr qreal kZSelectionHandles = 110.0;
inline constexpr qreal kZToolCreation = 200.0;
inline constexpr qreal kZTextLayer = 300.0;

} // namespace manga::ui::canvas
