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

    app.setFont(Typography::fontStyle(Typography::FontRole::Body).toQFont());

    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
        return app.exec();
    }

    return EXIT_SUCCESS;
}
