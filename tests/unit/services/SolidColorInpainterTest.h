#pragma once

#include <QObject>

class SolidColorInpainterTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void fillProducesExactPixelsAndBounds();
    void clampsRegionToPageBounds();
    void emptyIntersectionRejected();
    void clampedTinyRegionRejected();
    void invalidColorRejected();
    void invalidOpacityRejected();
    void preCancelledRequestReturnsCancelled();
};
