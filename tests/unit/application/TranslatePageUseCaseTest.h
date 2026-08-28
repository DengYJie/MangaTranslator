#pragma once
#include <QObject>

class TranslatePageUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulTranslation();
    void testTranslatorFailure();
    void testInvalidRequestDoesNotCallTranslator();
    void testCancellation();
};
