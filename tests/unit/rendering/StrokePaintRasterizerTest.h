#pragma once

#include <QObject>

class StrokePaintRasterizerTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void invalidInputLeavesImageUntouched();
    void singlePointPaintsRoundCap();
    void paintCompositesStraightAlphaOverDestination();
    void eraseScalesDestinationAlpha();
    void strokeIsClippedToImageBounds();
    void incrementalSegmentsMatchWholeStrokeExactly();
    void wholeStrokeReturnsUnionOfTouchedPixels();
};
