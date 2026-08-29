#include "ChatCompletionsProtocolTest.h"
#include "OpenAiCompatibleTranslatorTest.h"
#include "ProviderConfigTest.h"
#include "RetryPolicyTest.h"
#include "WindowsCredentialStoreTest.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;

    auto runSuite = [&](const char* name, QObject* testSuite) {
        std::cout << "\n================ Running " << name << " ================\n" << std::flush;
        const int res = QTest::qExec(
            testSuite, QStringList{QString::fromUtf8(argv[0]), QStringLiteral("-o"), QStringLiteral("-,txt")});
        if (res != 0) {
            std::cerr << "FAILED: " << name << " exited with status " << res << "\n" << std::flush;
            status |= res;
        }
    };

    {
        ProviderConfigTest suite;
        runSuite("ProviderConfigTest", &suite);
    }
    {
        ChatCompletionsProtocolTest suite;
        runSuite("ChatCompletionsProtocolTest", &suite);
    }
    {
        RetryPolicyTest suite;
        runSuite("RetryPolicyTest", &suite);
    }
    {
        OpenAiCompatibleTranslatorTest suite;
        runSuite("OpenAiCompatibleTranslatorTest", &suite);
    }
    {
        WindowsCredentialStoreTest suite;
        runSuite("WindowsCredentialStoreTest", &suite);
    }

    return status;
}
