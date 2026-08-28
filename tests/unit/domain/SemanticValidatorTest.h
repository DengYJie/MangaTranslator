#pragma once

#include <QObject>

class SemanticValidatorTest : public QObject {
    Q_OBJECT

private slots:
    void testValidProject();
    void testInvalidProjectId();
    void testEmptyProjectName();
    void testUnsupportedVersions();
    void testDuplicatePageIds();
    void testDuplicateRegionIds();
    void testDuplicateOrderIndices();
    void testOutOfBoundsRegion();
    void testInvalidCandidateSelection();
    void testInvalidLayoutAndClear();
};
