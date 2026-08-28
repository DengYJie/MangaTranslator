#pragma once

#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

class ITranslator {
public:
    virtual ~ITranslator() = default;

    virtual QFuture<Result<TranslationResponse>> translate(TranslationRequest request, OperationContext context) = 0;
};

} // namespace manga::application
