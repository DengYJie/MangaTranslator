#pragma once
#include <QObject>
class TextLayoutEngineTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void layoutHorizontalWrapsCorrectly();
    void layoutHorizontalHonorsPageCoordinatesAndDpi();
    void layoutVerticalHasCorrectBounds();
    void autoFitFindsBestSize();
    void fixedSizeReportsOverflow();
    void reportsFontFallbackAndMissingGlyph();
};
