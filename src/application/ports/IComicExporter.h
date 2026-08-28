#pragma once

#include "application/dto/ExportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IComicExporter {
public:
    virtual ~IComicExporter() = default;

    virtual QFuture<Result<ExportResponse>> exportPages(ExportRequest request, OperationContext context) = 0;
};

} // namespace manga::application
