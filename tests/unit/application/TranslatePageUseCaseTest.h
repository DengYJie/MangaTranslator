#pragma once
#include <QObject>

class TranslatePageUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulTranslation();
    void testTranslatorFailure();
    void testInvalidRequestDoesNotCallTranslator();
    void testCancellation();
};
