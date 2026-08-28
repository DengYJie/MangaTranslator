#pragma once

#include <QObject>

class ProjectSerializerTest : public QObject {
    Q_OBJECT

private slots:
    // Roundtrip
    void testMinimalProjectRoundTrip();
    void testFullProjectRoundTripWithDisabledGlossary();
    // isDirty lifecycle
    void testLoadedProjectIsClean();
    void testPersistedDirtyFieldIsRejected();
    // Structural validation (pre/post migration pipeline)
    void testMalformedJson();
    void testMissingVersionFields();
    void testUnsupportedSchemaVersion();
    void testUnsupportedFormatVersion();
    void testAdditionalPropertiesRejection();
    void testSettingsFieldTypeError();
    void testOptionalSchemaFieldsAreStrictlyValidated();
    // orderIndex validation (strict, no silent fix)
    void testPageOrderIndexMismatch();
    void testRegionOrderIndexMismatch();
    void testPageOrderIndexDuplicate();
    void testRegionOrderIndexDuplicate();
    // Geometry
    void testInvalidGeometryType();
    void testOutOfBoundsGeometryInJson();
    void testGeometryUnknownFieldRejected();
    // Source / Translation / Layout / Clear
    void testSourceUnknownFieldRejected();
    void testSourceConfidenceOutOfRange();
    void testTranslationUnknownFieldRejected();
    void testCandidateMissingIdOrText();
    void testCandidateConfidenceOutOfRange();
    void testLayoutMissingAlignment();
    void testLayoutMissingMargins();
    void testLayoutUnknownFieldRejected();
    // Semantic
    void testDuplicateIdsInJson();
    void testInvalidCandidateSelectionInJson();
    void testNonV4UuidInJson();
};
