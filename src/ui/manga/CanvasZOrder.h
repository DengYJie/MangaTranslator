#pragma once

#include <QtGlobal>

/// Frozen z-order for the manga canvas scene (phase-5-ui-editor-supplement §5).
/// V0.1 has no Split Curtain and reserves no curtain slot.
namespace manga::ui::canvas {

inline constexpr qreal kZBackground = 0.0;
inline constexpr qreal kZPageOriginal = 10.0;
inline constexpr qreal kZCleaningLayer = 20.0;
inline constexpr qreal kZTextLayer = 30.0;
inline constexpr qreal kZRegionNormal = 100.0;
inline constexpr qreal kZSelectionHandles = 110.0;
inline constexpr qreal kZToolCreation = 200.0;

} // namespace manga::ui::canvas
