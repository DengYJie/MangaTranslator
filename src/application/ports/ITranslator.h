#pragma once

#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {
/// If the translator depends on ICredentialStore or other ApplicationContext dependencies,
/// those dependencies MUST outlive all jobs started by this translator. The composition root
/// should await or cancel in-flight tasks before destroying the store.
class ITranslator {
public:
    virtual ~ITranslator() = default;

    virtual QFuture<Result<TranslationResponse>> translate(TranslationRequest request, OperationContext context) = 0;
};

} // namespace manga::application
