#include "app/Application.h"
#include "core/Logger.h"

#include <QStandardPaths>
#include <QStringLiteral>

int main(int argc, char* argv[]) {
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    manga::core::initLogger(logDir);

    manga::app::Application application(argc, argv);
    int result = application.run();

    manga::core::shutdownLogger();
    return result;
}
