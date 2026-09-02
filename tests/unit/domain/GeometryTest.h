#pragma once

#include <QObject>

class GeometryTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testRectValidity();
    void testRectWithinBounds();
    void testRectClamping();
    void testMarginsValidity();
    void testImageSize();
};
