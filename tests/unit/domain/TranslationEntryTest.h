#pragma once

#include <QObject>

class TranslationEntryTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testDefaultState();
    void testModelTextOnly();
    void testCandidatesWithoutSelection();
    void testCandidateSelection();
    void testManualOverridePriority();
    void testManualTextEmptyFallback();
    void testInvalidCandidateSelection();
    void testReset();
};
