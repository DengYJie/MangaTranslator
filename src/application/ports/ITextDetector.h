#pragma once

#include "application/dto/TextDetectionDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Abstract port for asynchronous comic layout analysis and text detection.
class ITextDetector {
public:
    virtual ~ITextDetector() = default;

    [[nodiscard]] virtual QFuture<Result<TextDetectionResponse>> detect(TextDetectionRequest request,
                                                                        OperationContext context) = 0;
};

} // namespace manga::application
