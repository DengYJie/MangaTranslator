#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    virtual QFuture<Result<OcrResponse>> recognize(OcrRequest request, OperationContext context) = 0;
};

} // namespace manga::application
