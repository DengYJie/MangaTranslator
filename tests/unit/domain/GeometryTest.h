#pragma once

#include <QObject>

class GeometryTest : public QObject {
    Q_OBJECT

private slots:
    void testRectValidity();
    void testRectWithinBounds();
    void testRectClamping();
    void testMarginsValidity();
    void testImageSize();
};
