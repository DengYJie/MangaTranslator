#include "TranslationEntryTest.h"

#include "domain/model/TranslationEntry.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void TranslationEntryTest::testDefaultState() {
    const TranslationEntry entry;
    QVERIFY(entry.effectiveText().empty());
    QVERIFY(!entry.hasManualText());
    QVERIFY(!entry.modelText().has_value());
    QVERIFY(entry.candidates().empty());
    QVERIFY(!entry.selectedCandidateId().has_value());
}

void TranslationEntryTest::testModelTextOnly() {
    TranslationEntry entry;
    entry.setModelResult("Model translation");

    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Model translation");
    QCOMPARE(entry.hasManualText(), false);
}

void TranslationEntryTest::testCandidatesWithoutSelection() {
    TranslationEntry entry;
    entry.setModelResult("Model translation",
                         {{.id = "c1", .text = "Candidate 1", .confidence = 0.9, .provider = "openai"},
                          {.id = "c2", .text = "Candidate 2", .confidence = 0.8, .provider = "openai"}});

    // Invariant: DO NOT auto-pick first candidate; fallback to model text
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Model translation");
}

void TranslationEntryTest::testCandidateSelection() {
    TranslationEntry entry;
    entry.setModelResult("Model translation",
                         {{.id = "c1", .text = "Candidate 1", .confidence = 0.9, .provider = "openai"},
                          {.id = "c2", .text = "Candidate 2", .confidence = 0.8, .provider = "openai"}});

    QVERIFY(entry.selectCandidate("c2"));
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Candidate 2");

    const auto sel = entry.selectedCandidate();
    QVERIFY(sel.has_value());
    QCOMPARE(QString::fromStdString(sel->text), "Candidate 2");
}

void TranslationEntryTest::testManualOverridePriority() {
    TranslationEntry entry;
    entry.setModelResult("Model translation",
                         {{.id = "c1", .text = "Candidate 1", .confidence = 0.9, .provider = "openai"}});
    entry.selectCandidate("c1");
    entry.setManualText("Manual human translation");

    // Invariant: manualText overrides both candidate and modelText
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Manual human translation");
    QVERIFY(entry.hasManualText());

    // Clearing manual text falls back to selected candidate
    entry.clearManualText();
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Candidate 1");

    // Clearing candidate selection falls back to model text
    entry.clearSelectedCandidate();
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Model translation");
}

void TranslationEntryTest::testManualTextEmptyFallback() {
    TranslationEntry entry;
    entry.setModelResult("Model translation");
    entry.setManualText(""); // Empty manual text should not mask model text

    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Model translation");
}

void TranslationEntryTest::testInvalidCandidateSelection() {
    TranslationEntry entry;
    entry.setModelResult("Model translation",
                         {{.id = "c1", .text = "Candidate 1", .confidence = 0.9, .provider = "openai"}});

    // Selecting non-existent candidate ID fails and does not change selection
    QVERIFY(!entry.selectCandidate("c_unknown"));
    QCOMPARE(QString::fromStdString(entry.effectiveText()), "Model translation");
}

void TranslationEntryTest::testReset() {
    TranslationEntry entry;
    entry.setModelResult("Model translation");
    entry.setManualText("Manual");
    entry.reset();

    QVERIFY(entry.effectiveText().empty());
    QVERIFY(!entry.hasManualText());
    QVERIFY(!entry.modelText().has_value());
}
