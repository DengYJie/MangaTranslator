#include "core/Logger.h"

#include "core/DebugOutputSink.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QVector>

namespace manga::tests {
namespace {

QVector<QString> gMirroredLines;

void recordMirror(const QString& line) {
    gMirroredLines.append(line);
}

QStringList readLogLines(const QString& directory) {
    const QStringList names = QDir(directory).entryList({QStringLiteral("*.log")}, QDir::Files);
    if (names.size() != 1) {
        return {};
    }

    QFile file(QDir(directory).filePath(names.first()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

} // namespace

class LoggerTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // The logger owns one process-wide handler and file, so each case initializes it, emits one
    // probe line, and shuts it down again instead of relying on case order.
    void init() {
        gMirroredLines.clear();
    }

    void cleanup() {
        core::shutdownLogger();
    }

    void mirroredLineMatchesTheLoggedFileLine();
    void logFileKeepsTheDocumentedLineFormatThroughTheDefaultSink();
    void nullMirrorSinkStillWritesTheLogFile();
};

void LoggerTest::mirroredLineMatchesTheLoggedFileLine() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString marker = QStringLiteral("[LoggerTest] mirror probe");
    core::initLogger(directory.path(), &recordMirror);
    qWarning().noquote() << marker;
    core::shutdownLogger();

    QCOMPARE(gMirroredLines.size(), 1);
    const QString mirrored = gMirroredLines.constFirst();
    QVERIFY(mirrored.contains(marker));
    QVERIFY(mirrored.contains(QStringLiteral("[WARNING]")));
    QVERIFY2(readLogLines(directory.path()).contains(mirrored), qPrintable(mirrored));
}

void LoggerTest::logFileKeepsTheDocumentedLineFormatThroughTheDefaultSink() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString marker = QStringLiteral("[LoggerTest] format probe");
    core::initLogger(directory.path());
    qWarning().noquote() << marker;
    core::shutdownLogger();

    const QStringList lines = readLogLines(directory.path());
    QCOMPARE(lines.size(), 1);
    const QString logged = lines.last();
    static const QRegularExpression format(QStringLiteral(
        R"(^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}] \[Thread:0x[0-9a-f]+] \[WARNING] \[default] .+ \(.+:\d+, .+\)$)"));
    QVERIFY2(logged.contains(marker), qPrintable(logged));
    QVERIFY2(format.match(logged).hasMatch(), qPrintable(logged));
}

void LoggerTest::nullMirrorSinkStillWritesTheLogFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString marker = QStringLiteral("[LoggerTest] unmirrored probe");
    core::initLogger(directory.path(), nullptr);
    qWarning().noquote() << marker;
    core::shutdownLogger();

    QVERIFY(gMirroredLines.isEmpty());
    bool found = false;
    const QStringList lines = readLogLines(directory.path());
    for (const QString& line : lines) {
        found = found || line.contains(marker);
    }
    QVERIFY(found);
}

} // namespace manga::tests

QTEST_MAIN(manga::tests::LoggerTest)
#include "LoggerTest.moc"
