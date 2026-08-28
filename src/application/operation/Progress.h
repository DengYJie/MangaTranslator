#pragma once

#include "application/operation/OperationStage.h"

#include <QtGlobal>

namespace manga::application {

struct Progress {
    OperationStage stage{OperationStage::Project};
    qint64 completed{0};
    qint64 total{0};
};

} // namespace manga::application
