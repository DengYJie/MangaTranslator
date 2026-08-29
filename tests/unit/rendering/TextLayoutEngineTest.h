#pragma once
#include <QObject>
class TextLayoutEngineTest : public QObject {
    Q_OBJECT
private slots:
    void layoutHorizontalWrapsCorrectly();
    void layoutVerticalHasCorrectBounds();
    void autoFitFindsBestSize();
};
