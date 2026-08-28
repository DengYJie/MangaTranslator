#pragma once
#include <QObject>

class TextUseCasesTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulSourceTextEdit();
    void testSourceTextEditFailsOnStaleRevision();
    void testSuccessfulTranslationEdit();
    void testTranslationEditFailsOnStaleRevision();
    void testSuccessfulCandidateSelection();
    void testCandidateSelectionFailsOnInvalidCandidate();
    void testCandidateSelectionFailsOnStaleRevision();
};
