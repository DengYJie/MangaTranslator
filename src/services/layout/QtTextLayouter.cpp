#include "services/layout/QtTextLayouter.h"

#include "rendering/TextLayoutEngine.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QPromise>

namespace manga::services {

QtTextLayouter::QtTextLayouter(BoundedTaskExecutor* executor) : m_executor(executor) {}

QtTextLayouter::~QtTextLayouter() = default;

QFuture<application::Result<application::LayoutResponse>>
QtTextLayouter::layout(application::LayoutRequest request, application::OperationContext context) {
    auto submitRes = m_executor->submit<application::LayoutResponse>(
        application::OperationStage::Layout,
        TaskQueue::Interactive,
        context,
        [request, context](const application::OperationContext&,
                           application::TaskProgress) -> application::Result<application::LayoutResponse> {
            // Check cancellation before work
            if (context.stopToken.stop_requested()) {
                return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                         "cancelled.user_requested",
                                                                         "Task was cancelled by the user."));
            }

            rendering::LayoutResult engineResult = rendering::TextLayoutEngine::layoutText(
                request.text, request.bounds, request.constraints, request.logicalDpi);

            // Check cancellation after work
            if (context.stopToken.stop_requested()) {
                return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                         "cancelled.user_requested",
                                                                         "Task was cancelled by the user."));
            }

            application::LayoutResponse response;
            response.regionId = request.regionId;
            response.resolvedLayout = engineResult.resolvedLayout;
            response.overflow = engineResult.overflow;

            for (const auto& w : engineResult.warnings) {
                response.warnings.append(application::LayoutWarning{w.code, w.safeMessage});
            }

            return response;
        });

    if (submitRes.has_value()) {
        return submitRes.value();
    } else {
        QPromise<application::Result<application::LayoutResponse>> promise;
        promise.start();
        promise.addResult(std::unexpected(submitRes.error()));
        promise.finish();
        return promise.future();
    }
}

} // namespace manga::services
