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

/// P5-B subset of the frozen EditorState (supplement §4): no regions, no tools, no
/// preview mode yet. Only stable IDs and value data; pixmaps never enter State.
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
