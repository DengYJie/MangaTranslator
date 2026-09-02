#pragma once

#include "application/dto/PageInpaintDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IPageInpainter {
public:
    virtual ~IPageInpainter() = default;

    [[nodiscard]] virtual QFuture<Result<PageInpaintResponse>> inpaint(PageInpaintRequest request,
                                                                       OperationContext context) = 0;

    /// Releases loaded model sessions and weights if no tasks are actively running.
    virtual void release() {}
};

} // namespace manga::application
