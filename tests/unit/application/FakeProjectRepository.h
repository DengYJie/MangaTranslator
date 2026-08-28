#pragma once

#include "application/ports/IProjectRepository.h"

#include <QPromise>

#include <memory>

namespace manga::application::test {

class FakeProjectRepository : public IProjectRepository {
public:
    ProjectDescriptor nextDescriptor;
    OpenProjectResult nextOpenResult;
    ApplicationError nextError;
    bool shouldFail{false};
    int callCount{0};
    int openCallCount{0};
    int recoverCallCount{0};

    template <typename T> QFuture<Result<T>> makeFuture(const T& val, const OperationContext& context) {
        ++callCount;
        auto promise = std::make_shared<QPromise<Result<T>>>();
        auto future = promise->future();

        if (context.stopToken.stop_requested()) {
            promise->addResult(std::unexpected(makeApplicationError(ErrorCategory::Cancelled, "cancelled", "")));
        } else if (shouldFail) {
            promise->addResult(std::unexpected(nextError));
        } else {
            promise->addResult(val);
        }
        promise->finish();
        return future;
    }

    QFuture<Result<void>> makeVoidFuture(const OperationContext& context) {
        ++callCount;
        auto promise = std::make_shared<QPromise<Result<void>>>();
        auto future = promise->future();

        if (context.stopToken.stop_requested()) {
            promise->addResult(std::unexpected(makeApplicationError(ErrorCategory::Cancelled, "cancelled", "")));
        } else if (shouldFail) {
            promise->addResult(std::unexpected(nextError));
        } else {
            promise->addResult({});
        }
        promise->finish();
        return future;
    }

    QFuture<Result<ProjectDescriptor>> createProject(CreateProjectRequest, OperationContext context) override {
        return makeFuture(nextDescriptor, context);
    }

    QFuture<Result<OpenProjectResult>> openProject(OpenProjectRequest, OperationContext context) override {
        ++openCallCount;
        return makeFuture(nextOpenResult, context);
    }

    QFuture<Result<OpenProjectResult>> recoverProject(OpenProjectRequest, OperationContext context) override {
        ++recoverCallCount;
        return makeFuture(nextOpenResult, context);
    }

    QFuture<Result<void>> saveProject(SaveProjectRequest, OperationContext context) override {
        return makeVoidFuture(context);
    }

    QFuture<Result<ProjectDescriptor>> saveProjectAs(SaveAsProjectRequest, OperationContext context) override {
        return makeFuture(nextDescriptor, context);
    }

    QFuture<Result<void>> closeProject(CloseProjectRequest, OperationContext context) override {
        return makeVoidFuture(context);
    }
};

} // namespace manga::application::test
