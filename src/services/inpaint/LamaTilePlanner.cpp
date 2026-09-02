#include "LamaTilePlanner.h"

#include <QImage>
#include <QRect>
#include <QVector>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace manga::services {
namespace {

// Mask values at or above this threshold are removed by the model.
constexpr int kBinaryThreshold = 127;

// Half-open bounds `[left, right) x [top, bottom)` in the mask's local coordinate space.
// Koharu packs and clips with this representation, and keeping it internal avoids off-by-one
// drift against Qt's inclusive `QRect::right()`.
struct Bounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] int width() const {
        return right - left;
    }

    [[nodiscard]] int height() const {
        return bottom - top;
    }

    [[nodiscard]] bool isEmpty() const {
        return right <= left || bottom <= top;
    }

    [[nodiscard]] quint64 area() const {
        return isEmpty() ? quint64(0) : quint64(right - left) * quint64(bottom - top);
    }
};

[[nodiscard]] Bounds boundsOf(const QRect& rect) {
    return Bounds{rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height()};
}

[[nodiscard]] QRect rectOf(const Bounds& bounds) {
    return QRect(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
}

[[nodiscard]] Bounds unionBounds(const Bounds& outer, const Bounds& inner) {
    return Bounds{
        std::min(outer.left, inner.left),
        std::min(outer.top, inner.top),
        std::max(outer.right, inner.right),
        std::max(outer.bottom, inner.bottom),
    };
}

[[nodiscard]] Bounds intersectBounds(const Bounds& first, const Bounds& second) {
    return Bounds{
        std::max(first.left, second.left),
        std::max(first.top, second.top),
        std::min(first.right, second.right),
        std::min(first.bottom, second.bottom),
    };
}

[[nodiscard]] bool fitsExtent(const Bounds& bounds, int extent) {
    return bounds.width() <= extent && bounds.height() <= extent;
}

struct Component {
    quint32 label = 0;
    Bounds bounds;
};

struct PlannedTile {
    QVector<quint32> labels;
    Bounds core;
    Bounds crop;
};

// Labels the binarised mask with eight-connected components whose identifiers start at one,
// recording each component's tight bounds. Flood fill is iterative because a manga mask can
// contain far more connected pixels than the native call stack can carry.
// `labels` receives the label grid, which later stages probe for per-cell tight bounds.
[[nodiscard]] std::vector<Component> labelComponents(const QImage& mask, std::vector<quint32>& labels) {
    const int width = mask.width();
    const int height = mask.height();
    const std::size_t pixelCount = std::size_t(width) * std::size_t(height);
    labels.assign(pixelCount, 0);

    std::vector<Component> components;
    std::vector<std::size_t> pending;

    for (int seedY = 0; seedY < height; ++seedY) {
        const auto* seedRow = mask.constScanLine(seedY);
        for (int seedX = 0; seedX < width; ++seedX) {
            if (seedRow[seedX] < kBinaryThreshold) {
                continue;
            }
            const std::size_t seedIndex = std::size_t(seedY) * std::size_t(width) + std::size_t(seedX);
            if (labels[seedIndex] != 0) {
                continue;
            }

            const quint32 label = quint32(components.size()) + 1;
            Bounds bounds{seedX, seedY, seedX + 1, seedY + 1};
            labels[seedIndex] = label;
            pending.push_back(seedIndex);

            while (!pending.empty()) {
                const std::size_t index = pending.back();
                pending.pop_back();
                const int y = int(index / std::size_t(width));
                const int x = int(index - std::size_t(y) * std::size_t(width));
                bounds.left = std::min(bounds.left, x);
                bounds.top = std::min(bounds.top, y);
                bounds.right = std::max(bounds.right, x + 1);
                bounds.bottom = std::max(bounds.bottom, y + 1);

                const int rowStart = std::max(y - 1, 0);
                const int rowEnd = std::min(y + 1, height - 1);
                const int columnStart = std::max(x - 1, 0);
                const int columnEnd = std::min(x + 1, width - 1);
                for (int neighbourY = rowStart; neighbourY <= rowEnd; ++neighbourY) {
                    const auto* neighbourRow = mask.constScanLine(neighbourY);
                    const std::size_t rowBase = std::size_t(neighbourY) * std::size_t(width);
                    for (int neighbourX = columnStart; neighbourX <= columnEnd; ++neighbourX) {
                        if (neighbourRow[neighbourX] < kBinaryThreshold) {
                            continue;
                        }
                        const std::size_t neighbourIndex = rowBase + std::size_t(neighbourX);
                        if (labels[neighbourIndex] != 0) {
                            continue;
                        }
                        labels[neighbourIndex] = label;
                        pending.push_back(neighbourIndex);
                    }
                }
            }

            components.push_back(Component{label, bounds});
        }
    }

    return components;
}

// Tight bounds of `label` inside `region`, or nothing when the label has no pixel there.
[[nodiscard]] std::optional<Bounds>
componentBoundsIn(const std::vector<quint32>& labels, int width, quint32 label, const Bounds& region) {
    int left = region.right;
    int top = region.bottom;
    int right = 0;
    int bottom = 0;

    for (int y = region.top; y < region.bottom; ++y) {
        const quint32* row = labels.data() + std::size_t(y) * std::size_t(width);
        for (int x = region.left; x < region.right; ++x) {
            if (row[x] != label) {
                continue;
            }
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }

    const Bounds bounds{left, top, right, bottom};
    if (bounds.isEmpty()) {
        return std::nullopt;
    }
    return bounds;
}

[[nodiscard]] int ceilModulo(int value, int modulo) {
    if (value % modulo == 0) {
        return value;
    }
    return (value / modulo + 1) * modulo;
}

// Maps a padded index back into `[0, length)` by reflecting about the last real pixel, so the
// reflection never repeats an edge column.
[[nodiscard]] int symmetricIndex(int index, int length) {
    const int period = length * 2;
    const int wrapped = index % period;
    return wrapped < length ? wrapped : period - wrapped - 1;
}

} // namespace

std::vector<LamaTile> planLamaTiles(const QImage& mask, const QRect& imageRect, int tileCore, int tileContext) {
    if (mask.isNull() || tileCore <= 0) {
        return {};
    }

    const QImage grayMask =
        mask.format() == QImage::Format_Grayscale8 ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    if (grayMask.isNull() || grayMask.width() <= 0 || grayMask.height() <= 0) {
        return {};
    }

    const Bounds maskBounds{0, 0, grayMask.width(), grayMask.height()};
    Bounds pageBounds = boundsOf(imageRect);
    if (pageBounds.isEmpty()) {
        pageBounds = maskBounds;
    }
    const Bounds clipBounds = intersectBounds(maskBounds, pageBounds);
    if (clipBounds.isEmpty()) {
        return {};
    }

    const int context = std::max(tileContext, 0);
    const auto growCrop = [context, clipBounds](const Bounds& core) {
        return Bounds{
            std::max(core.left - context, clipBounds.left),
            std::max(core.top - context, clipBounds.top),
            std::min(core.right + context, clipBounds.right),
            std::min(core.bottom + context, clipBounds.bottom),
        };
    };
    const auto makeTile = [](quint32 label, const Bounds& core, const Bounds& crop) {
        PlannedTile tile;
        tile.labels.push_back(label);
        tile.core = core;
        tile.crop = crop;
        return tile;
    };

    std::vector<quint32> labels;
    const std::vector<Component> components = labelComponents(grayMask, labels);

    std::vector<PlannedTile> boundedTiles;
    std::vector<PlannedTile> splitTiles;

    for (const Component& component : components) {
        if (fitsExtent(component.bounds, tileCore)) {
            int bestIndex = -1;
            quint64 bestGrowth = 0;
            Bounds bestCore;
            for (std::size_t index = 0; index < boundedTiles.size(); ++index) {
                const Bounds merged = unionBounds(boundedTiles[index].core, component.bounds);
                if (!fitsExtent(merged, tileCore)) {
                    continue;
                }
                const quint64 growth = merged.area() - boundedTiles[index].core.area();
                if (bestIndex < 0 || growth < bestGrowth) {
                    bestIndex = int(index);
                    bestGrowth = growth;
                    bestCore = merged;
                }
            }

            if (bestIndex >= 0) {
                PlannedTile& tile = boundedTiles[std::size_t(bestIndex)];
                tile.labels.push_back(component.label);
                tile.core = bestCore;
                tile.crop = growCrop(bestCore);
            } else {
                boundedTiles.push_back(makeTile(component.label, component.bounds, growCrop(component.bounds)));
            }
            continue;
        }

        for (int cellTop = component.bounds.top; cellTop < component.bounds.bottom;) {
            const int cellBottom = std::min(cellTop + tileCore, component.bounds.bottom);
            for (int cellLeft = component.bounds.left; cellLeft < component.bounds.right;) {
                const int cellRight = std::min(cellLeft + tileCore, component.bounds.right);
                const Bounds cell{cellLeft, cellTop, cellRight, cellBottom};
                const std::optional<Bounds> owned = componentBoundsIn(labels, grayMask.width(), component.label, cell);
                if (owned.has_value()) {
                    // The cell stays the writable core while the crop follows the tight extent, so a
                    // thin diagonal stroke does not drag a full tile-size context along.
                    splitTiles.push_back(makeTile(component.label, cell, growCrop(*owned)));
                }
                cellLeft = cellRight;
            }
            cellTop = cellBottom;
        }
    }

    boundedTiles.insert(boundedTiles.end(), splitTiles.begin(), splitTiles.end());
    // Stable so tiles with an identical core keep packing order; Koharu's sort is stable too.
    std::stable_sort(boundedTiles.begin(), boundedTiles.end(), [](const PlannedTile& first, const PlannedTile& second) {
        if (first.core.top != second.core.top) {
            return first.core.top < second.core.top;
        }
        if (first.core.left != second.core.left) {
            return first.core.left < second.core.left;
        }
        if (first.core.bottom != second.core.bottom) {
            return first.core.bottom < second.core.bottom;
        }
        return first.core.right < second.core.right;
    });

    std::vector<LamaTile> tiles;
    tiles.reserve(boundedTiles.size());
    for (const PlannedTile& planned : boundedTiles) {
        tiles.push_back(LamaTile{rectOf(planned.core), rectOf(planned.crop), planned.labels});
    }
    return tiles;
}

void buildSymmetricPadIndices(int length, int padMultiple, std::vector<int>& indices) {
    if (length <= 0) {
        indices.clear();
        return;
    }
    if (padMultiple <= 1) {
        indices.resize(std::size_t(length));
        for (int index = 0; index < length; ++index) {
            indices[std::size_t(index)] = index;
        }
        return;
    }

    const int paddedLength = ceilModulo(length, padMultiple);
    indices.resize(std::size_t(paddedLength));
    for (int index = 0; index < paddedLength; ++index) {
        indices[std::size_t(index)] = symmetricIndex(index, length);
    }
}

} // namespace manga::services
