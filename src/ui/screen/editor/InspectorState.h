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
    AutoFit,
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
    bool autoFit{false};

    bool operator==(const SingleRegionInspectorState&) const = default;
};

/// Multi-region selection: common styling properties without text or candidates.
struct MultiRegionInspectorState {
    domain::PageId pageId;
    QVector<domain::RegionId> regionIds;
    int regionCount{0};

    std::optional<QString> fontFamily;
    std::optional<int> fontWeight;
    std::optional<double> fontSizePt;
    std::optional<double> letterSpacingEm;
    std::optional<domain::TextOrientation> orientation;
    std::optional<domain::HorizontalAlignment> horizontalAlignment;
    std::optional<domain::VerticalAlignment> verticalAlignment;
    std::optional<double> lineSpacing;
    std::optional<double> marginTop;
    std::optional<double> marginRight;
    std::optional<double> marginBottom;
    std::optional<double> marginLeft;
    std::optional<QString> textColorHex;
    std::optional<QString> strokeColorHex;
    std::optional<double> strokeWidthPx;
    std::optional<bool> autoFit;

    bool allLocked{false};

    bool operator==(const MultiRegionInspectorState&) const = default;
};

/// Inspector surface projection: empty (monostate), single region, or multi-region.
using InspectorState = std::variant<std::monostate, SingleRegionInspectorState, MultiRegionInspectorState>;

} // namespace manga::ui::screen::editor

Q_DECLARE_METATYPE(manga::ui::screen::editor::InspectorField)
Q_DECLARE_METATYPE(QVector<manga::domain::RegionId>)
