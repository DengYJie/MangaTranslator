#pragma once

#include "application/dto/ImportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IComicImporter {
public:
    virtual ~IComicImporter() = default;

    virtual QFuture<Result<ImportImagesResult>> importImages(ImportImagesRequest request, OperationContext context) = 0;
};

} // namespace manga::application
