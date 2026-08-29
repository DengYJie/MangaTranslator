#pragma once

#include "application/ports/ITextLayouter.h"

namespace manga::services {

class BoundedTaskExecutor;

class QtTextLayouter final : public application::ITextLayouter {
public:
    explicit QtTextLayouter(BoundedTaskExecutor* executor);
    ~QtTextLayouter() override;

    QFuture<application::Result<application::LayoutResponse>> layout(application::LayoutRequest request,
                                                                     application::OperationContext context) override;

private:
    BoundedTaskExecutor* m_executor;
};

} // namespace manga::services
