#pragma once

#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {
/// Translates one request through a remote provider. Dependencies other than the request are read
/// on the calling thread, so the returned future is already valid when this method returns.
class ITranslator {
public:
    virtual ~ITranslator() = default;

    /// Starts one provider request and fulfills the returned future when the provider answers or the
    /// transport fails. Dependency presence and the provider configuration are checked on the
    /// calling thread, and a missing credential store, transport, or configuration fulfills the
    /// future on that same thread with internal.unavailable_dependency. A fulfilled request carries
    /// the provider category codes the transport maps, and a rejected payload carries a model
    /// category code. The implementation observes no stop token, so a started request runs to its
    /// network completion and the caller can only discard the result. The request and context are
    /// taken by value, the translator does not own the credential store it reads, and every in-flight
    /// job must finish before that store is destroyed.
    virtual QFuture<Result<TranslationResponse>> translate(TranslationRequest request, OperationContext context) = 0;
};

} // namespace manga::application
