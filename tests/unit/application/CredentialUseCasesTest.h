#pragma once

#include <QtTest/QtTest>

class CredentialUseCasesTest : public QObject {
    Q_OBJECT
private slots:
    void testCheckCredentialStatus();
    void testSetCredential();
    void testClearCredential();
    void testValidation();
};
