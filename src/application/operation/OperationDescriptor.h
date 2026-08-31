#pragma once

#include "application/operation/OperationStage.h"
#include "domain/model/Uuid.h"

#include <QString>

#include <optional>

namespace manga::application {

struct OperationDescriptor {
    OperationStage stage;
    domain::ProjectId projectId;
    domain::PageId pageId;
    std::optional<domain::RegionId> regionId;
    QString displayTarget;
    bool cancellable{true};
};

} // namespace manga::application
