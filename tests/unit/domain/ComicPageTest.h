#pragma once

#include <QObject>

class ComicPageTest : public QObject {
    Q_OBJECT

private slots:
    void testPageCreationAndDefaults();
    void testAddAndRemoveRegion();
    void testCreationBoundaryRejection();
    void testUpdateRegionGeometryClamping();
    void testLoadRegionPreservesOrderIndex();
    void testLoadRegionRejectsWrongOrderIndex();
    void testExplicitUpdateRegionSource();
    void testExplicitUpdateRegionTranslation();
    void testExplicitUpdateRegionLayout();
    void testSetRegionFlagsDontNeedUpdateCallback();
    void testNormalizeRegionOrder();
    void testImageReplacementInvariant();
};
