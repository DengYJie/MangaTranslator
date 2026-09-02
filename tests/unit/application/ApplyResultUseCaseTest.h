#pragma once

#include <QObject>

class ApplyResultUseCaseTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testApplyOcrSuccess();
    void testApplyOcrFailsOnProjectMismatch();
    void testApplyOcrFailsOnPageMismatch();
    void testApplyOcrFailsOnRegionMismatch();
    void testApplyOcrFailsOnStaleRevision();
    void testApplyOcrFailsOnStaleGeneration();
    void testApplyOcrFailsOnManualOverride();

    void testApplyTranslationSuccess();
    void testApplyTranslationFailsOnDuplicateId();
    void testApplyTranslationReportsPartialFailures();
    void testApplyTranslationReportsSourceMismatch();
    void testApplyTranslationFailsOnStaleGeneration();
    void testApplyTranslationRejectsStaleAndManualTargets();

    void testApplyClearSuccess();
    void testApplyClearSupportsUndoAndRedo();
    void testApplyClearRejectsStaleRevision();
    void testApplyClearPropagatesCommitFailure();
    void testApplyClearRejectsPatchBoundsMismatch();
    void testApplyLayoutSuccess();
    void testApplyLayoutRejectsStaleGeneration();

private:
    class Private;
    Private* d;
};
