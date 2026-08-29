#pragma once

#include "application/dto/ClearDto.h"
#include "application/ports/IInpainter.h"
#include "services/execution/BoundedTaskExecutor.h"

#include <QFuture>

namespace manga::services {

/// V0.1 clear engine: fills the affected region with the ClearSettings color and returns an
/// independent RGBA8 patch. No auto color picking, no file writes, no Domain access; the
/// caller composites the patch onto the page layer via ApplyClearResultUseCase.
class SolidColorInpainter final : public application::IInpainter {
public:
    explicit SolidColorInpainter(BoundedTaskExecutor* executor);

    [[nodiscard]] QFuture<application::Result<application::ClearResponse>>
    clear(application::ClearRequest request, application::OperationContext context) override;

private:
    BoundedTaskExecutor* m_executor;
};

} // namespace manga::services
