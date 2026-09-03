#include "Logger.h"

#include "core/DebugOutputSink.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTextStream>
#include <QThread>

#include <iostream>

namespace manga::core {
namespace {

QFile* g_logFile = nullptr;
DebugSink g_debugSink = nullptr;
QMutex g_logMutex;
QtMessageHandler g_originalHandler = nullptr;

void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    QMutexLocker locker(&g_logMutex);

    QString levelStr;
    switch (type) {
        case QtDebugMsg:
            levelStr = QStringLiteral("DEBUG");
            break;
        case QtInfoMsg:
            levelStr = QStringLiteral("INFO");
            break;
        case QtWarningMsg:
            levelStr = QStringLiteral("WARNING");
            break;
        case QtCriticalMsg:
            levelStr = QStringLiteral("CRITICAL");
            break;
        case QtFatalMsg:
            levelStr = QStringLiteral("FATAL");
            break;
    }

    QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    QString file = context.file ? QString::fromUtf8(context.file) : QStringLiteral("unknown");
    QString function = context.function ? QString::fromUtf8(context.function) : QStringLiteral("unknown");
    int line = context.line;

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);

    // Format: [yyyy-MM-dd HH:mm:ss.zzz] [Thread:0x<tid>] [LEVEL] [Category] Message (file:line, function)
    QString formattedMsg =
        QStringLiteral("[%1] [Thread:0x%2] [%3] [%4] %5 (%6:%7, %8)")
            .arg(timestamp, threadId, levelStr, category, msg, file, QString::number(line), function);

    std::cerr << formattedMsg.toLocal8Bit().constData() << std::endl;

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << formattedMsg << "\n";
        out.flush();
        g_logFile->flush();
    }

    if (g_debugSink) {
        g_debugSink(formattedMsg);
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

void initLogger(const QString& logDirPath, DebugSink debugSink) {
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        return;
    }

    QDir().mkpath(logDirPath);

    QString dateStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    QString logFilePath = QDir(logDirPath).filePath(QStringLiteral("manga_%1.log").arg(dateStr));

    g_logFile = new QFile(logFilePath);
    if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        g_debugSink = debugSink;
        g_originalHandler = qInstallMessageHandler(customMessageHandler);
    } else {
        delete g_logFile;
        g_logFile = nullptr;
    }
}

void shutdownLogger() {
    QMutexLocker locker(&g_logMutex);
    if (g_logFile) {
        qInstallMessageHandler(g_originalHandler);
        g_originalHandler = nullptr;
        g_debugSink = nullptr;

        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

} // namespace manga::core
