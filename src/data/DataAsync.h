#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/AsyncResult.h"

#include <QFuture>
#include <QPromise>
#include <QThreadPool>

#include <exception>
#include <memory>
#include <utility>

namespace manga::data {

/// Runs fn on the global thread pool and fulfills a Result future with its return value.
/// fn must be nullary and return application::Result<T>. Exceptions are caught at the
/// adapter boundary and never cross an application port.
template <typename T, typename Fn> [[nodiscard]] QFuture<application::Result<T>> runDataOperation(Fn&& fn) {
    auto promise = std::make_shared<QPromise<application::Result<T>>>();
    promise->start();
    auto future = promise->future();
    QThreadPool::globalInstance()->start([promise, fn = std::forward<Fn>(fn)]() mutable {
        try {
            promise->addResult(fn());
        } catch (const std::exception&) {
            promise->addResult(std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Internal,
                                                  QStringLiteral("internal.unexpected_exception"),
                                                  QStringLiteral("An unexpected internal error occurred"))));
        } catch (...) {
            promise->addResult(std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Internal,
                                                  QStringLiteral("internal.unexpected_exception"),
                                                  QStringLiteral("An unexpected internal error occurred"))));
        }
        promise->finish();
    });
    return future;
}

} // namespace manga::data
