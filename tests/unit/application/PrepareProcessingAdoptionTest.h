#pragma once

#include <QObject>

class PrepareProcessingAdoptionTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testIdMismatch();
    void testStaleGeneration();
    void testDeletedRegion();
    void testContentRevisionConflict();
    void testManualSourceOverride();
    void testManualTranslationOverride();
    void testTranslationUnknownOrMissingId();
    void testTranslationDuplicateId();
    void testLayoutWarningOrOverflow();
    void testClearPrepareIoFailure();
    void testPipelinePreparesAllStages();

private:
    class PrepareProcessingAdoptionTestPrivate* d_ptr{nullptr};
};
