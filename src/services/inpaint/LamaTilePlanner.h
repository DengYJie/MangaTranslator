#pragma once

#include <QImage>
#include <QRect>
#include <QVector>

#include <vector>

namespace manga::services {

/// One LaMa inference unit.
///
/// `crop` is the region fed to the model, including surrounding context used by the
/// Fast Fourier Convolutions. `core` is the subset whose pixels this tile may write.
/// `labels` holds the connected-component identifiers this tile is responsible for, so
/// overlapping crops cannot clobber each other's output during writeback.
struct LamaTile {
    QRect core;
    QRect crop;
    QVector<quint32> labels;
};

/// Plans inference tiles for an 8-bit grayscale mask where values >= 127 mean remove.
///
/// `mask` uses coordinates local to its own extent, and `imageRect` bounds the region the
/// returned crops may sample from. Components that fit `tileCore` are packed together into
/// the fewest tiles by smallest union area growth; oversized components are split on a
/// `tileCore` grid and each cell is cropped tight to that component's real extent. Each crop
/// grows by `tileContext` and is clipped to `imageRect`. Tiles are sorted by top, left,
/// bottom, right so progress is monotonic and results are deterministic. Returns an empty
/// vector when the mask has no pixel >= 127.
[[nodiscard]] std::vector<LamaTile>
planLamaTiles(const QImage& mask, const QRect& imageRect, int tileCore, int tileContext);

/// Fills `indices` with `ceilModulo(length, padMultiple)` entries mapping each padded index
/// back into `[0, length)` by symmetric reflection about the last real pixel, matching the
/// padding that IOPaint applies before LaMa inference.
void buildSymmetricPadIndices(int length, int padMultiple, std::vector<int>& indices);

} // namespace manga::services
