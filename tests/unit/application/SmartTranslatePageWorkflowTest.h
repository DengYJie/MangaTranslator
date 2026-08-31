#pragma once

#include <QObject>

class SmartTranslatePageWorkflowTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init();
    void cleanup();

    void testNoRegionsYieldsSkipped();
    void testSuccessfulPipeline();
    void testFailsAtDependencyStopsSubsequent();
    void testCancelOperation();
    void testManualOverrideSkipsProcessing();
    void testPartialSuccessRetainsProposals();
    void testSessionGenerationChangeYieldsStale();

private:
};
