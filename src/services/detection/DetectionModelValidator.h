#pragma once

#include "application/ports/IDetectionModelValidator.h"

namespace manga::services {

class DetectionModelValidator final : public application::IDetectionModelValidator {
public:
    DetectionModelValidator() = default;
    ~DetectionModelValidator() override = default;

    [[nodiscard]] application::Result<application::DetectionModelStatus>
    validate(const QString& modelDirectory) override;
};

} // namespace manga::services
