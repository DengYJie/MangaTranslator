#pragma once

#include "application/dto/ExportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/ports/IComicExporter.h"
#include "domain/model/ComicProject.h"

#include <QFuture>

#include <optional>

namespace manga::application {

enum class ExportScope {
    CurrentPage,
    SelectedPages,
    AllPages
};

struct ExportPagesRequest {
    domain::ProjectId projectId;
    ExportScope scope{ExportScope::AllPages};
    QString outputDir;
    ExportFormat format{ExportFormat::Png};
    int jpegQuality{95};
    QVector<ExportPageSnapshot> pageSnapshots;
    std::optional<domain::PageId> currentPageId;
    QVector<domain::PageId> selectedPageIds;
};

class ExportPagesUseCase {
public:
    explicit ExportPagesUseCase(IComicExporter* exporter, domain::ComicProject* project)
        : m_exporter(exporter), m_project(project) {}

    [[nodiscard]] QFuture<Result<ExportResponse>> execute(const ExportPagesRequest& request,
                                                          const OperationContext& context);

private:
    IComicExporter* m_exporter;
    domain::ComicProject* m_project;
};

} // namespace manga::application
