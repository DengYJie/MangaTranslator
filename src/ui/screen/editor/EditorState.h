#pragma once

#include "application/dto/ProjectDto.h"
#include "domain/model/Geometry.h"
#include "domain/model/Uuid.h"
#include "ui/base/DomainMetaTypes.h"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::editor {

/// Renderable projection of one project page. No pixmaps, no domain pointers.
struct EditorPageEntry {
    domain::PageId id;
    int orderIndex{0};
    QString displayName;
    domain::ImageSize imageSize;
    int regionCount{0};

    [[nodiscard]] double aspectRatio() const {
        return imageSize.isValid() ? static_cast<double>(imageSize.width) / imageSize.height : 1.0;
    }
    bool operator==(const EditorPageEntry&) const = default;
};

/// Page-level recoverable problem surfaced by the canvas placeholder.
struct EditorNotice {
    QString safeMessage;
    bool retryable{false};
    bool operator==(const EditorNotice&) const = default;
};

/// Canvas interaction tool.
enum class CanvasTool {
    Select,
    Pan,
    CreateRegion
};

/// Page preview mode for comparing original and translated content.
enum class PreviewMode {
    Original,
    Translated,
    Compare
};

/// Selection behavior when clicking a region.
enum class RegionSelectionMode {
    Replace,
    Toggle,
    Range
};

/// Inspector panel mode derived from selection count.
enum class InspectorMode {
    Page,
    Single,
    Multi
};

/// Value-typed projection of one text region on the current page.
struct RegionProjection {
    domain::RegionId id;
    domain::RectGeometry geometry;
    int orderIndex{0};
    quint64 contentRevision{0};
    bool isLocked{false};
    bool isCleared{false};
    bool hasManualTranslation{false};

    bool operator==(const RegionProjection&) const = default;
};

/// Document save and undo state projection. Stays false until the undo command
/// integration wires the project stack; until then canUndo and canRedo remain
/// false and texts are plain defaults.
struct DocumentSaveState {
    bool isDirty{false};
    bool isSaving{false};
    QString undoText{QStringLiteral("Undo")};
    QString redoText{QStringLiteral("Redo")};
    std::optional<QString> saveError;

    bool operator==(const DocumentSaveState&) const = default;
};

/// Frozen editor state snapshot. All value-typed, no domain pointers or pixmaps.
struct EditorState {
    domain::ProjectId projectId{}; // nil = no project open
    QString projectName;
    QVector<EditorPageEntry> pages;
    domain::PageId currentPageId{}; // nil = no current page
    double zoomFactor{1.0};
    quint64 viewGeneration{0};
    quint64 projectSessionGeneration{0};
    bool isPageLoading{false};
    std::optional<EditorNotice> notice;

    /// Current page region projections sorted by orderIndex.
    QVector<RegionProjection> regions;
    /// Selected region IDs sorted by page region order.
    QVector<domain::RegionId> selectedRegionIds;
    /// Primary region ID (always a member of selectedRegionIds; nil when empty).
    domain::RegionId primaryRegionId{};
    /// Active canvas tool.
    CanvasTool activeTool{CanvasTool::Select};
    /// Page preview mode.
    PreviewMode previewMode{PreviewMode::Original};
    /// Inspector mode derived from selection count.
    InspectorMode inspectorMode{InspectorMode::Page};

    /// Mirrors undo and redo availability on the project stack.
    bool canUndo{false};
    bool canRedo{false};
    /// Set true when a command replay reports internal.undo_invariant_broken.
    /// While faulted, further undo/redo intents are no-ops until the session resets.
    bool undoHistoryFaulted{false};
    /// Document save state projection.
    DocumentSaveState saveState;

    bool operator==(const EditorState&) const = default;
};

/// One-shot delivery of a decoded page image to the canvas. Never stored in State.
struct PageImageDelivery {
    domain::PageId pageId;
    QImage image;
    double zoom{1.0};
    QPointF centerOnPage;
    bool hasSavedTransform{false};
};

} // namespace manga::ui::screen::editor

Q_DECLARE_METATYPE(manga::ui::screen::editor::PageImageDelivery)
