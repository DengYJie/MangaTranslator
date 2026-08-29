#include "ApplicationUseCasesTest.h"
#include "ApplyResultUseCaseTest.h"
#include "ClearRegionUseCaseTest.h"
#include "CredentialUseCasesTest.h"
#include "ExportPagesUseCaseTest.h"
#include "ImportImagesUseCaseTest.h"
#include "LayoutRegionUseCaseTest.h"
#include "LoadPageImageUseCaseTest.h"
#include "ProjectUseCasesTest.h"
#include "RecognizeRegionUseCaseTest.h"
#include "RegionUseCasesTest.h"
#include "TaskProgressTest.h"
#include "TextUseCasesTest.h"
#include "TranslatePageUseCaseTest.h"

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
        RecognizeRegionUseCaseTest suite;
        runSuite("RecognizeRegionUseCaseTest", &suite);
    }
    {
        ApplyResultUseCaseTest suite;
        runSuite("ApplyResultUseCaseTest", &suite);
    }
    {
        TranslatePageUseCaseTest suite;
        runSuite("TranslatePageUseCaseTest", &suite);
    }
    {
        ClearRegionUseCaseTest suite;
        runSuite("ClearRegionUseCaseTest", &suite);
    }
    {
        LayoutRegionUseCaseTest suite;
        runSuite("LayoutRegionUseCaseTest", &suite);
    }
    {
        ImportImagesUseCaseTest suite;
        runSuite("ImportImagesUseCaseTest", &suite);
    }
    {
        ExportPagesUseCaseTest suite;
        runSuite("ExportPagesUseCaseTest", &suite);
    }
    {
        LoadPageImageUseCaseTest suite;
        runSuite("LoadPageImageUseCaseTest", &suite);
    }
    {
        RegionUseCasesTest suite;
        runSuite("RegionUseCasesTest", &suite);
    }
    {
        ApplicationUseCasesTest suite;
        runSuite("ApplicationUseCasesTest", &suite);
    }
    {
        TextUseCasesTest suite;
        runSuite("TextUseCasesTest", &suite);
    }
    {
        TaskProgressTest suite;
        runSuite("TaskProgressTest", &suite);
    }
    {
        ProjectUseCasesTest suite;
        runSuite("ProjectUseCasesTest", &suite);
    }
    {
        CredentialUseCasesTest suite;
        runSuite("CredentialUseCasesTest", &suite);
    }

    return status;
}
