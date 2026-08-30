#pragma once
#include <QObject>
class TextLayoutEngineTest : public QObject {
    Q_OBJECT
private slots:
    void layoutHorizontalWrapsCorrectly();
    void layoutHorizontalHonorsPageCoordinatesAndDpi();
    void layoutVerticalHasCorrectBounds();
    void autoFitFindsBestSize();
    void fixedSizeReportsOverflow();
    void reportsFontFallbackAndMissingGlyph();
};
