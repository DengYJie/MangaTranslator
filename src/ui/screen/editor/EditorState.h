#pragma once

#include "application/dto/ProjectDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationId.h"
#include "application/operation/OperationStage.h"
#include "domain/model/Geometry.h"
#include "domain/model/TextLayout.h"
#include "domain/model/Uuid.h"
#include "ui/base/DomainMetaTypes.h"
#include "ui/screen/editor/InspectorState.h"

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
    QString actionText;
    application::RecoveryAction action{application::RecoveryAction::None};
    bool retryable{false};
    bool operator==(const EditorNotice&) const = default;
};

/// Canvas interaction tool.
enum class CanvasTool {
    Select,
    Pan,
    Text,
    Remove,
    Brush,
    Eraser
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
    None,
    Single,
    Multi
};

/// Value-typed projection of one text region on the current page.
struct RegionProjection {
    domain::RegionId id;
    domain::RectGeometry geometry;
    std::optional<domain::RectGeometry> bubbleGeometry;
    QString effectiveTranslation;
    domain::TextLayout layout;
    bool isEnabled{true};
    bool isTranslationVisible{true};
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

/// Processing status for background tasks and proposals.
enum class ProcessingStatus {
    Idle,
    Queued,
    Running,
    NeedsReview,
    Completed,
    Failed,
    Cancelled
};

/// Region-level processing status projection.
struct RegionProcessingStatus {
    domain::RegionId regionId;
    ProcessingStatus status{ProcessingStatus::Idle};
    application::OperationStage stage{application::OperationStage::Ocr};
    QString errorSummary;
    bool hasConflict{false};

    bool operator==(const RegionProcessingStatus&) const = default;
};

/// Projection of asynchronous processing, review, and proposal state.
/// Holds only operation IDs, target IDs, stage/status, safe issue summaries,
/// and proposal availability. Never stores raw result images.
struct EditorProcessingState {
    domain::PageId activePageId{};
    application::OperationId activeOperationId{};
    application::OperationStage stage{application::OperationStage::Ocr};
    ProcessingStatus status{ProcessingStatus::Idle};
    bool isRunning{false};
    bool hasProposal{false};
    QString safeMessage;
    QVector<domain::RegionId> targetRegionIds;
    QVector<domain::RegionId> needsReviewRegionIds;
    QVector<domain::RegionId> failedRegionIds;
    QVector<domain::RegionId> runningRegionIds;
    QVector<domain::RegionId> conflictRegionIds;
    QVector<RegionProcessingStatus> regionStatuses;

    bool operator==(const EditorProcessingState&) const = default;
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
    std::optional<EditorNotice> exportNotice;
    QVector<QString> exportFailureSummaries;
    bool isOcrAvailable{true};
    QString ocrUnavailableReason;
    bool isDetectionAvailable{true};
    QString detectionUnavailableReason;
    bool isTranslationAvailable{true};
    QString translationUnavailableReason;
    bool isInpaintAvailable{false};
    QString inpaintUnavailableReason;

    /// Current page region projections sorted by orderIndex.
    QVector<RegionProjection> regions;
    /// Selected region IDs sorted by page region order.
    QVector<domain::RegionId> selectedRegionIds;
    /// Primary region ID (always a member of selectedRegionIds; nil when empty).
    domain::RegionId primaryRegionId{};
    /// Active canvas tool.
    CanvasTool activeTool{CanvasTool::Select};
    /// Current brush color and diameter.
    QColor brushColor{17, 17, 17, 255};
    double brushDiameter{32.0};
    /// Page preview mode.
    PreviewMode previewMode{PreviewMode::Translated};
    /// Inspector mode derived from selection count.
    InspectorMode inspectorMode{InspectorMode::None};
    /// Inspector surface projection driven by selectedRegionIds.
    InspectorState inspector;

    /// Mirrors undo and redo availability on the project stack.
    bool canUndo{false};
    bool canRedo{false};
    /// Set true when a command replay reports internal.undo_invariant_broken.
    /// While faulted, further undo/redo intents are no-ops until the session resets.
    bool undoHistoryFaulted{false};
    /// Document save state projection.
    DocumentSaveState saveState;
    /// Asynchronous processing and review workflow state projection.
    EditorProcessingState processing;

    /// True while a page-range export runs; duplicate export intents are ignored.
    bool isExporting{false};
    /// Batch page selection for export commands; may be in any user order.
    QVector<domain::PageId> selectedPageIds;

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
