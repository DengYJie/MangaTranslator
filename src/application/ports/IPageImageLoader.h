#pragma once

#include "application/dto/ImportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>
#include <QImage>

namespace manga::application {

class IPageImageLoader {
public:
    virtual ~IPageImageLoader() = default;

    virtual QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest request, OperationContext context) = 0;
};

} // namespace manga::application
