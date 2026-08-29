#pragma once

#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/SolidColorInpainter.h"

#include <QObject>

#include <memory>

class SolidColorInpainterTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void fillProducesExactPixelsAndBounds();
    void clampsRegionToPageBounds();
    void emptyIntersectionRejected();
    void clampedTinyRegionRejected();
    void nonFiniteRegionRejected();
    void invalidColorRejected();
    void invalidOpacityRejected();
    void preCancelledRequestReturnsCancelled();

private:
    std::unique_ptr<manga::services::BoundedTaskExecutor> m_executor;
    std::unique_ptr<manga::services::SolidColorInpainter> m_inpainter;
};
