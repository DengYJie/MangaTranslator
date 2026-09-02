#pragma once

#include <QObject>

class ComicPageTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
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
    void testInsertRegionAtBeginning();
    void testInsertRegionInMiddle();
    void testInsertRegionAtEnd();
    void testInsertRegionDuplicateIdLeavesPageUnmodified();
    void testInsertRegionOutOfRangeOrderLeavesPageUnmodified();
    void testInsertRegionInvalidGeometryLeavesPageUnmodified();
    void testInsertRegionMaintainsContiguousOrder();
};
