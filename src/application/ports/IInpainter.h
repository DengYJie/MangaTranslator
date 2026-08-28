#pragma once

#include "application/dto/ClearDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IInpainter {
public:
    virtual ~IInpainter() = default;

    virtual QFuture<Result<ClearResponse>> clear(ClearRequest request, OperationContext context) = 0;
};

} // namespace manga::application
