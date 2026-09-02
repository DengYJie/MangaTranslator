#pragma once

#include "application/ports/IPageInpainter.h"
#include "services/inpaint/InpaintManifest.h"

#include <memory>

namespace Ort {
struct Env;
}

namespace manga::services {
class BoundedTaskExecutor;

class OnnxLamaPageInpainter final : public application::IPageInpainter {
public:
    OnnxLamaPageInpainter(Ort::Env* env, BoundedTaskExecutor* executor, InpaintManifest manifest);
    ~OnnxLamaPageInpainter() override;

    [[nodiscard]] QFuture<application::Result<application::PageInpaintResponse>>
    inpaint(application::PageInpaintRequest request, application::OperationContext context) override;

    void release() override;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;

    [[nodiscard]] application::Result<void> ensureLoaded();
};

} // namespace manga::services
