#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/ReadingDirection.h"
#include "domain/model/TextContent.h"
#include "domain/model/TextLayout.h"
#include "domain/model/Uuid.h"

#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::editor {

/// Editable Inspector fields carried by panel edit signals. Stable identifier;
/// never carries widget pointers.
enum class InspectorField {
    SourceText,
    ManualTranslation,
    FontFamily,
    FontWeight,
    FontSizePt,
    LetterSpacingEm,
    LineSpacing,
    Orientation,
    HorizontalAlignment,
    VerticalAlignment,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    TextColorHex,
    StrokeColorHex,
    StrokeWidthPx,
    Cleared
};

/// Renderable projection of one translation candidate.
struct InspectorCandidateProjection {
    QString id;
    QString text;
    double confidence{1.0};
    QString provider;
    bool isSelected{false};

    bool operator==(const InspectorCandidateProjection&) const = default;
};

/// Single selection: full region projection including text, candidates and TextLayout.
struct SingleRegionInspectorState {
    domain::PageId pageId;
    domain::RegionId regionId;

    QString sourceText;
    domain::TextSourceType sourceType{domain::TextSourceType::PureManual};
    double sourceConfidence{1.0};
    QString manualTranslation; // empty when no manual override
    bool hasManualTranslation{false};
    QString effectiveTranslation; // TranslationEntry::effectiveText priority rules
    QVector<InspectorCandidateProjection> candidates;

    bool isLocked{false};
    bool isCleared{false};
    domain::RectGeometry geometry;
    int orderIndex{0};

    QString fontFamily;
    double fontSizePt{14.0};
    int fontWeight{400};
    double letterSpacingEm{0.0};
    domain::TextOrientation orientation{domain::TextOrientation::Horizontal};
    domain::HorizontalAlignment horizontalAlignment{domain::HorizontalAlignment::Center};
    domain::VerticalAlignment verticalAlignment{domain::VerticalAlignment::Middle};
    double lineSpacing{1.2};
    domain::Margins margins;
    QString textColorHex;
    QString strokeColorHex;
    double strokeWidthPx{0.0};

    bool operator==(const SingleRegionInspectorState&) const = default;
};

/// Inspector surface projection: active single region projection or nullopt when unselected.
using InspectorState = std::optional<SingleRegionInspectorState>;

} // namespace manga::ui::screen::editor

Q_DECLARE_METATYPE(manga::ui::screen::editor::InspectorField)
Q_DECLARE_METATYPE(QVector<manga::domain::RegionId>)
