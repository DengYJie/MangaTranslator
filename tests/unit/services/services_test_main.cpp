#include "BoundedTaskExecutorTest.h"

#include <QCoreApplication>
#include <QtTest/QtTest>

#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    int status = 0;

    auto runSuite = [&](const char* name, QObject* testSuite) {
        std::cout << "\n================ Running " << name << " ================\n" << std::flush;
        const int res = QTest::qExec(testSuite, argc, argv);
        if (res != 0) {
            std::cerr << "FAILED: " << name << " exited with status " << res << "\n" << std::flush;
            status |= res;
        }
    };

    {
        BoundedTaskExecutorTest suite;
        runSuite("BoundedTaskExecutorTest", &suite);
    }

    return status;
}
