#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/TextLayout.h"
#include "domain/model/Uuid.h"

#include <QString>

namespace manga::ui::manga {

struct TextLayerDisplay {
    domain::RegionId id;
    domain::RectGeometry geometry;
    QString text;
    domain::TextLayout layout;
    bool enabled{true};
    bool visible{true};
    double logicalDpi{72.0};
    bool operator==(const TextLayerDisplay&) const = default;
};

} // namespace manga::ui::manga
