#include "FileSystemComicImporterTest.h"
#include "FileSystemProjectRepositoryTest.h"
#include "ProjectSerializerTest.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
#include "data/project/FileSystemProjectRepository.h"

#include <QCoreApplication>
#include <QLockFile>
#include <QtTest/QtTest>

#include <cstring>
#include <iostream>

// Runs before any test suite when the stale-lock test spawns a child process: the child
// acquires the project lock and exits without unwinding, leaving a lock file with a dead
// process behind (the QLockFile destructor never runs).
static int lockAndCrash(const QString& projectRoot) {
    QLockFile lock(projectRoot + QStringLiteral("/.mangatranslator.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(1000)) {
        return 3;
    }
    std::_Exit(0);
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc >= 3 && std::strcmp(argv[1], "--lock-and-crash") == 0) {
        return lockAndCrash(QString::fromLocal8Bit(argv[2]));
    }

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
        ProjectSerializerTest suite;
        runSuite("ProjectSerializerTest", &suite);
    }
    {
        FileSystemProjectRepositoryTest suite;
        runSuite("FileSystemProjectRepositoryTest", &suite);
    }
    {
        FileSystemComicImporterTest suite;
        runSuite("FileSystemComicImporterTest", &suite);
    }

    return status;
}
