#pragma once

#include "application/dto/LayoutDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class ITextLayouter {
public:
    virtual ~ITextLayouter() = default;

    virtual QFuture<Result<LayoutResponse>> layout(LayoutRequest request, OperationContext context) = 0;
};

} // namespace manga::application
