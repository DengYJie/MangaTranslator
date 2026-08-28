#include "ClearSettingsTest.h"
#include "ComicPageTest.h"
#include "ComicProjectTest.h"
#include "GeometryTest.h"
#include "SemanticValidatorTest.h"
#include "TextLayoutTest.h"
#include "TranslationEntryTest.h"
#include "UuidTest.h"

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
        UuidTest suite;
        runSuite("UuidTest", &suite);
    }
    {
        GeometryTest suite;
        runSuite("GeometryTest", &suite);
    }
    {
        TranslationEntryTest suite;
        runSuite("TranslationEntryTest", &suite);
    }
    {
        TextLayoutTest suite;
        runSuite("TextLayoutTest", &suite);
    }
    {
        ClearSettingsTest suite;
        runSuite("ClearSettingsTest", &suite);
    }
    {
        ComicPageTest suite;
        runSuite("ComicPageTest", &suite);
    }
    {
        ComicProjectTest suite;
        runSuite("ComicProjectTest", &suite);
    }
    {
        SemanticValidatorTest suite;
        runSuite("SemanticValidatorTest", &suite);
    }

    return status;
}
