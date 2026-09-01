#pragma once

#include "application/ports/IInpaintModelValidator.h"

namespace manga::services {

class InpaintModelValidator final : public application::IInpaintModelValidator {
public:
    [[nodiscard]] application::Result<application::InpaintModelStatus> validate(const QString& modelDirectory) override;
};

} // namespace manga::services
