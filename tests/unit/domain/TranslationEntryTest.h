#pragma once

#include <QObject>

class TranslationEntryTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaultState();
    void testModelTextOnly();
    void testCandidatesWithoutSelection();
    void testCandidateSelection();
    void testManualOverridePriority();
    void testManualTextEmptyFallback();
    void testInvalidCandidateSelection();
    void testReset();
};
