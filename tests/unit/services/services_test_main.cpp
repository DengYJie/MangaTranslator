#include "BaberuOcrIntegrationTest.h"
#include "BaberuOcrTest.h"
#include "BoundedTaskExecutorTest.h"
#include "OcrModelValidatorTest.h"
#include "QtTextLayouterTest.h"
#include "SolidColorInpainterTest.h"

#include <QGuiApplication>
#include <QtTest/QtTest>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

bool optionTakesValue(const QByteArray& argument) {
    static const QByteArrayList kValueOptions = {QByteArrayLiteral("-o"),
                                                 QByteArrayLiteral("--output"),
                                                 QByteArrayLiteral("-input"),
                                                 QByteArrayLiteral("--datatags")};
    return kValueOptions.contains(argument);
}

/// Qt Test fails a suite when it is handed a function name that suite does not declare, so foreign names
/// are dropped before running. Option values stay in their original position because arguments such as
/// "-o file,format" carry the value in the following entry.
template <class Suite> void runIfSelected(int& status, const char* name, const std::vector<QByteArray>& arguments) {
    const QMetaObject& meta = Suite::staticMetaObject;
    const bool filtered =
        std::ranges::any_of(arguments, [](const QByteArray& argument) { return !argument.startsWith('-'); });

    std::vector<QByteArray> owned;
    bool isOptionValue = false;
    for (const QByteArray& argument : arguments) {
        const bool keepsArgument =
            isOptionValue || argument.startsWith('-') || meta.indexOfSlot(argument.constData()) >= 0;
        if (keepsArgument) {
            owned.push_back(argument);
        }
        isOptionValue = !argument.startsWith('-') ? false : optionTakesValue(argument);
    }

    // A filter list that matches no function of this suite skips the suite entirely.
    const bool ownsAnyFunction = std::ranges::any_of(owned, [&meta](const QByteArray& argument) {
        return !argument.startsWith('-') && meta.indexOfSlot(argument.constData()) >= 0;
    });
    if (filtered && !ownsAnyFunction) {
        return;
    }

    std::vector<char*> argv;
    argv.reserve(owned.size());
    for (QByteArray& argument : owned) {
        argv.push_back(argument.data());
    }

    std::cout << "\n================ Running " << name << " ================\n" << std::flush;
    Suite suite;
    const int result = QTest::qExec(&suite, static_cast<int>(argv.size()), argv.data());
    if (result != 0) {
        std::cerr << "FAILED: " << name << " exited with status " << result << "\n" << std::flush;
        status |= result;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    std::vector<QByteArray> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i], qstrlen(argv[i]));
    }

    int status = 0;
    runIfSelected<OcrModelValidatorTest>(status, "OcrModelValidatorTest", arguments);
    runIfSelected<BoundedTaskExecutorTest>(status, "BoundedTaskExecutorTest", arguments);
    runIfSelected<SolidColorInpainterTest>(status, "SolidColorInpainterTest", arguments);
    runIfSelected<QtTextLayouterTest>(status, "QtTextLayouterTest", arguments);
    runIfSelected<manga::services::test::BaberuOcrTest>(status, "BaberuOcrTest", arguments);
    runIfSelected<manga::services::test::BaberuOcrIntegrationTest>(status, "BaberuOcrIntegrationTest", arguments);

    return status;
}
