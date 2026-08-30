#include "FileSystemThumbnailStoreTest.h"

#include "application/ports/IThumbnailStore.h"
#include "data/derived/FileSystemThumbnailStore.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QImageReader>
#include <QtTest/QtTest>

#include <stop_token>

using namespace manga;

namespace {

template <typename T> application::Result<T> await(QFuture<application::Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

QImage solidImage(const QColor& color) {
    QImage image(32, 16, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

QString relativePath(const domain::PageId& pageId) {
    return QStringLiteral("assets/derived/%1/thumbnail/v1.png").arg(QString::fromStdString(pageId.toString()));
}

} // namespace

void FileSystemThumbnailStoreTest::init() {
    QVERIFY(m_tempDir.isValid());
    m_coordinator = std::make_unique<data::ProjectIoCoordinator>();
    m_store = std::make_unique<data::FileSystemThumbnailStore>(m_coordinator.get());
}

void FileSystemThumbnailStoreTest::cleanup() {
    m_store.reset();
    m_coordinator.reset();
    QDir root(m_tempDir.path());
    QVERIFY(root.removeRecursively());
    QVERIFY(QDir().mkpath(m_tempDir.path()));
}

void FileSystemThumbnailStoreTest::roundTripUsesFixedAtomicPngPath() {
    const auto projectId = domain::ProjectId::generate();
    const auto pageId = domain::PageId::generate();
    QVERIFY(m_coordinator->bindProject(projectId, QFileInfo(m_tempDir.path()).canonicalFilePath()));

    application::WriteThumbnailRequest writeRequest{projectId, m_tempDir.path(), pageId, solidImage(Qt::red)};
    QVERIFY(await(m_store->writeThumbnail(std::move(writeRequest), {})).has_value());

    const QString path = QDir(m_tempDir.path()).filePath(relativePath(pageId));
    QVERIFY(QFileInfo::exists(path));
    QImageReader reader(path);
    QCOMPARE(reader.format().toLower(), QByteArrayLiteral("png"));

    application::ReadThumbnailRequest readRequest{projectId, m_tempDir.path(), pageId};
    auto read = await(m_store->readThumbnail(std::move(readRequest), {}));
    QVERIFY(read.has_value());
    QCOMPARE(read->size(), QSize(32, 16));
    QCOMPARE(read->pixelColor(0, 0), QColor(Qt::red));

    const QDir directory(QFileInfo(path).absolutePath());
    QCOMPARE(directory.entryList(QDir::Files | QDir::NoDotAndDotDot), QStringList{QStringLiteral("v1.png")});
}

void FileSystemThumbnailStoreTest::missingThumbnailReturnsStableError() {
    const auto projectId = domain::ProjectId::generate();
    const auto pageId = domain::PageId::generate();
    QVERIFY(m_coordinator->bindProject(projectId, QFileInfo(m_tempDir.path()).canonicalFilePath()));

    application::ReadThumbnailRequest request{projectId, m_tempDir.path(), pageId};
    auto result = await(m_store->readThumbnail(std::move(request), {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("thumbnail.not_found"));
}

void FileSystemThumbnailStoreTest::unboundProjectIsRejected() {
    application::WriteThumbnailRequest request{
        domain::ProjectId::generate(), m_tempDir.path(), domain::PageId::generate(), solidImage(Qt::red)};
    auto result = await(m_store->writeThumbnail(std::move(request), {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("thumbnail.commit_failed"));
}

void FileSystemThumbnailStoreTest::cancelledReplacementPreservesExistingThumbnail() {
    const auto projectId = domain::ProjectId::generate();
    const auto pageId = domain::PageId::generate();
    QVERIFY(m_coordinator->bindProject(projectId, QFileInfo(m_tempDir.path()).canonicalFilePath()));

    application::WriteThumbnailRequest initial{projectId, m_tempDir.path(), pageId, solidImage(Qt::red)};
    QVERIFY(await(m_store->writeThumbnail(std::move(initial), {})).has_value());

    std::stop_source stopSource;
    stopSource.request_stop();
    application::OperationContext context;
    context.stopToken = stopSource.get_token();
    application::WriteThumbnailRequest replacement{projectId, m_tempDir.path(), pageId, solidImage(Qt::blue)};
    auto cancelled = await(m_store->writeThumbnail(std::move(replacement), context));
    QVERIFY(!cancelled.has_value());
    QVERIFY(cancelled.error().isCancelled());

    application::ReadThumbnailRequest readRequest{projectId, m_tempDir.path(), pageId};
    auto read = await(m_store->readThumbnail(std::move(readRequest), {}));
    QVERIFY(read.has_value());
    QCOMPARE(read->pixelColor(0, 0), QColor(Qt::red));
}

#include "moc_FileSystemThumbnailStoreTest.cpp"
