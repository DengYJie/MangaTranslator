#pragma once

#include "application/workflow/OperationRegistry.h"
#include "data/derived/FileSystemDerivedAssetStore.h"
#include "data/derived/FileSystemThumbnailStore.h"
#include "data/export/FileSystemComicExporter.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/SolidColorInpainter.h"
#include "services/layout/QtTextLayouter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

namespace manga::test {

inline constexpr int kAwaitTimeoutMs = 10000;

/// Event-loop-driven bounded wait. Returns false on timeout; callers must QVERIFY the result
/// before reading future.result(). No unconditional sleeps anywhere in integration tests.
template <typename T> [[nodiscard]] bool awaitFuture(const QFuture<T>& future, int timeoutMs = kAwaitTimeoutMs) {
    if (future.isFinished()) {
        return true;
    }
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    timer.start(timeoutMs);
    loop.exec();
    return future.isFinished();
}

/// Real data/services adapters rooted at a temporary directory. The stack owns everything it
/// constructs; declaration order = teardown order.
struct RealDataStack {
    QTemporaryDir tempRoot;
    std::unique_ptr<data::ProjectIoCoordinator> ioCoordinator;
    std::unique_ptr<data::FileSystemProjectRepository> repository;
    std::unique_ptr<data::FileSystemComicImporter> importer;
    std::unique_ptr<data::FileSystemPageImageLoader> pageImageLoader;
    std::unique_ptr<data::FileSystemThumbnailStore> thumbnailStore;
    std::unique_ptr<data::FileSystemDerivedAssetStore> derivedAssetStore;
    std::unique_ptr<data::FileSystemComicExporter> exporter;
    std::unique_ptr<services::BoundedTaskExecutor> executor;
    std::unique_ptr<services::SolidColorInpainter> inpainter;
    std::unique_ptr<services::QtTextLayouter> layouter;
    application::OperationRegistry operationRegistry;

    RealDataStack()
        : ioCoordinator(std::make_unique<data::ProjectIoCoordinator>()),
          repository(std::make_unique<data::FileSystemProjectRepository>(ioCoordinator.get())),
          importer(std::make_unique<data::FileSystemComicImporter>(ioCoordinator.get())),
          pageImageLoader(std::make_unique<data::FileSystemPageImageLoader>(ioCoordinator.get())),
          thumbnailStore(std::make_unique<data::FileSystemThumbnailStore>(ioCoordinator.get())),
          derivedAssetStore(std::make_unique<data::FileSystemDerivedAssetStore>(ioCoordinator.get())),
          exporter(std::make_unique<data::FileSystemComicExporter>()),
          executor(std::make_unique<services::BoundedTaskExecutor>()),
          inpainter(std::make_unique<services::SolidColorInpainter>(executor.get())),
          layouter(std::make_unique<services::QtTextLayouter>(executor.get())) {}

    [[nodiscard]] QString projectRoot(const QString& name) const {
        return QDir(tempRoot.path()).filePath(name);
    }
};

/// Computes SHA-256 hashes for all files under root/subdir recursively.
/// The returned map maps relative file paths (relative to root/subdir) to their hex SHA-256 digests.
[[nodiscard]] inline QHash<QString, QString> sha256Tree(const QString& root, const QString& subdir = {}) {
    QHash<QString, QString> result;
    const QString targetPath = subdir.isEmpty() ? root : QDir(root).filePath(subdir);
    const QDir baseDir(targetPath);
    if (!baseDir.exists()) {
        return result;
    }
    QDirIterator it(targetPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFile file(it.filePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
            const QString relPath = baseDir.relativeFilePath(it.filePath());
            result.insert(relPath, QString::fromLatin1(hash));
        }
    }
    return result;
}

/// Creates a unique temporary project directory path.
[[nodiscard]] inline QString makeTempProjectRoot(const QString& scenarioName = QStringLiteral("scenario")) {
    const QString base = QDir::tempPath();
    const QString path = QDir(base).filePath(
        QStringLiteral("manga_test_%1_%2").arg(scenarioName, QString::number(QRandomGenerator::global()->generate())));
    QDir().mkpath(path);
    return path;
}

} // namespace manga::test
