#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <cstdlib>

int main(int argc, char* argv[]) {
    fluent::prepareHighDpiApplication();

    QApplication app(argc, argv);
    app.setApplicationName("MangaTranslator");
    app.setApplicationVersion("0.1.0");

    if (!fluent::initializeResources()) {
        return EXIT_FAILURE;
    }

    app.setFont(fluent::Typography::fontStyle(fluent::Typography::FontRole::Body).toQFont());
    [[maybe_unused]] auto baseLow = fluent::Color::systemBaseLowColor();
    [[maybe_unused]] auto depth = fluent::Elevation::depthForLevel(1);
    [[maybe_unused]] auto duration = fluent::Animation::slowDuration();
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        return app.exec();
    }

    return EXIT_SUCCESS;
}
