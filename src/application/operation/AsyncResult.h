#pragma once

#include "application/error/ApplicationError.h"

#include <QFuture>
#include <QPromise>

#include <utility>

namespace manga::application {

template <typename T> [[nodiscard]] QFuture<Result<T>> makeReadyFuture(Result<T> result) {
    QPromise<Result<T>> promise;
    auto future = promise.future();
    promise.addResult(std::move(result));
    promise.finish();
    return future;
}

template <typename T> [[nodiscard]] QFuture<Result<T>> makeFailedFuture(ApplicationError error) {
    return makeReadyFuture<T>(std::unexpected(std::move(error)));
}

} // namespace manga::application
