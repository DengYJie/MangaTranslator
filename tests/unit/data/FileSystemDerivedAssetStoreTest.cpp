#include "FileSystemDerivedAssetStoreTest.h"

#include "application/dto/ImportDto.h"
#include "data/derived/FileSystemDerivedAssetStore.h"
#include "data/project/ProjectIoCoordinator.h"
#include "domain/model/ComicPage.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

using namespace manga;
using namespace manga::application;
using domain::PageId;
using domain::ProjectId;
using manga::data::FileSystemDerivedAssetStore;
using manga::data::ProjectIoCoordinator;

namespace {

template <typename T> application::Result<T> await(QFuture<application::Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

QImage solidLayer(int width, int height, int red, int green, int blue, int alpha) {
    QImage layer(width, height, QImage::Format_RGBA8888);
    layer.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            layer.setPixel(x, y, qRgba(red, green, blue, alpha));
        }
    }
    return layer;
}

QByteArray readAllBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString pageDirectory(const PageId& pageId) {
    return QStringLiteral("assets/derived/%1").arg(QString::fromStdString(pageId.toString()));
}

} // namespace

void FileSystemDerivedAssetStoreTest::init() {
    QVERIFY(m_tempDir.isValid());
    m_coordinator = std::make_unique<ProjectIoCoordinator>();
    m_store = std::make_unique<FileSystemDerivedAssetStore>(m_coordinator.get());
    m_projectId = ProjectId::generate();
    m_pageId = PageId::generate();
    // Asset ports only serve projects the repository bound to the canonical root.
    QVERIFY(m_coordinator->bindProject(m_projectId, QFileInfo(m_tempDir.path()).canonicalFilePath()));
}

void FileSystemDerivedAssetStoreTest::cleanup() {
    m_store.reset();
    m_coordinator.reset();
}

std::optional<CommitDerivedLayerResponse> FileSystemDerivedAssetStoreTest::commit(const QImage& layer) {
    CommitDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.layer = layer;
    auto result = await(m_store->commitDerivedLayer(std::move(request), {}));
    if (!result) {
        qWarning() << "commitDerivedLayer failed:" << result.error().code;
        return std::nullopt;
    }
    return *result;
}

void FileSystemDerivedAssetStoreTest::commitCreatesContentAddressedPng() {
    const auto response = commit(solidLayer(4, 2, 255, 128, 0, 200));
    QVERIFY(response.has_value());

    // assets/derived/<page-id>/cleaning/v1/<64-hex>.png
    const QString prefix = pageDirectory(m_pageId) + QStringLiteral("/cleaning/v1/");
    QVERIFY(response->relativePath.startsWith(prefix));
    const QString fileName = response->relativePath.mid(prefix.size());
    QCOMPARE(fileName.size(), 68); // 64 hash chars + ".png"
    QVERIFY(fileName.endsWith(QStringLiteral(".png")));

    // The committed file decodes to the committed pixels.
    QImageReader reader(m_tempDir.path() + QStringLiteral("/") + response->relativePath);
    auto image = reader.read();
    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(0, 0), QColor(255, 128, 0, 200));
}

void FileSystemDerivedAssetStoreTest::identicalCommitReusesExistingFile() {
    const auto first = commit(solidLayer(4, 2, 10, 20, 30, 255));
    QVERIFY(first.has_value());
    const QDir derivedDir(m_tempDir.path() + QStringLiteral("/") + pageDirectory(m_pageId) +
                          QStringLiteral("/cleaning/v1"));
    QCOMPARE(derivedDir.entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);

    const auto second = commit(solidLayer(4, 2, 10, 20, 30, 255));
    QVERIFY(second.has_value());
    QCOMPARE(second->relativePath, first->relativePath);
    QCOMPARE(derivedDir.entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 1);
}

void FileSystemDerivedAssetStoreTest::differentContentCommitsSecondFileWithoutTouchingFirst() {
    const auto first = commit(solidLayer(4, 2, 10, 20, 30, 255));
    QVERIFY(first.has_value());
    const QString firstPath = m_tempDir.path() + QStringLiteral("/") + first->relativePath;
    const QByteArray firstBytes = readAllBytes(firstPath);
    QVERIFY(!firstBytes.isEmpty());

    const auto second = commit(solidLayer(4, 2, 200, 100, 50, 128));
    QVERIFY(second.has_value());
    QVERIFY(second->relativePath != first->relativePath);
    QCOMPARE(readAllBytes(firstPath), firstBytes);
}

void FileSystemDerivedAssetStoreTest::readRoundTripReturnsCommittedPixels() {
    const auto committed = commit(solidLayer(3, 5, 1, 2, 3, 250));
    QVERIFY(committed.has_value());

    ReadDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.relativePath = committed->relativePath;
    auto read = await(m_store->readDerivedLayer(std::move(request), {}));

    QVERIFY(read.has_value());
    QCOMPARE(read->size(), QSize(3, 5));
    QCOMPARE(read->pixelColor(2, 4), QColor(1, 2, 3, 250));
}

void FileSystemDerivedAssetStoreTest::readMissingLayerFails() {
    ReadDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.relativePath = pageDirectory(m_pageId) + QStringLiteral("/cleaning/v1/") + QStringLiteral("missing.png");
    auto read = await(m_store->readDerivedLayer(std::move(request), {}));

    QVERIFY(!read.has_value());
    QCOMPARE(read.error().code, QStringLiteral("clear.layer_read_failed"));
}

void FileSystemDerivedAssetStoreTest::readRejectsPathOutsideRequestedPage() {
    ReadDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = PageId::generate();
    request.layerKind = DerivedLayerKind::Clear;
    request.relativePath = pageDirectory(m_pageId) + QStringLiteral("/cleaning/v1/anything.png");
    auto read = await(m_store->readDerivedLayer(std::move(request), {}));

    QVERIFY(!read.has_value());
    QCOMPARE(read.error().code, QStringLiteral("clear.layer_read_failed"));
}

void FileSystemDerivedAssetStoreTest::commitRejectsNullOrWrongFormatLayer() {
    CommitDerivedLayerRequest nullRequest;
    nullRequest.projectId = m_projectId;
    nullRequest.projectRoot = m_tempDir.path();
    nullRequest.pageId = m_pageId;
    nullRequest.layerKind = DerivedLayerKind::Clear;
    nullRequest.layer = QImage();
    auto nullResult = await(m_store->commitDerivedLayer(std::move(nullRequest), {}));
    QVERIFY(!nullResult.has_value());
    QCOMPARE(nullResult.error().code, QStringLiteral("clear.layer_commit_failed"));

    CommitDerivedLayerRequest wrongFormat;
    wrongFormat.projectId = m_projectId;
    wrongFormat.projectRoot = m_tempDir.path();
    wrongFormat.pageId = m_pageId;
    wrongFormat.layerKind = DerivedLayerKind::Clear;
    wrongFormat.layer = QImage(4, 4, QImage::Format_RGB32);
    auto wrongResult = await(m_store->commitDerivedLayer(std::move(wrongFormat), {}));
    QVERIFY(!wrongResult.has_value());
    QCOMPARE(wrongResult.error().code, QStringLiteral("clear.layer_commit_failed"));
}

void FileSystemDerivedAssetStoreTest::commitRejectsUnboundProjectSession() {
    CommitDerivedLayerRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.layer = solidLayer(2, 2, 4, 5, 6, 255);
    auto result = await(m_store->commitDerivedLayer(std::move(request), {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("clear.layer_commit_failed"));
}

void FileSystemDerivedAssetStoreTest::stopRequestedCommitReturnsCancelled() {
    std::stop_source source;
    source.request_stop();
    application::OperationContext context;
    context.stopToken = source.get_token();

    CommitDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.layer = solidLayer(2, 2, 9, 9, 9, 255);
    auto result = await(m_store->commitDerivedLayer(std::move(request), context));

    QVERIFY(!result.has_value());
    QVERIFY(result.error().isCancelled());
}

void FileSystemDerivedAssetStoreTest::corruptedExistingFileFailsAndIsNeverOverwritten() {
    const QImage original = solidLayer(4, 2, 10, 20, 30, 255);
    const auto first = commit(original);
    QVERIFY(first.has_value());

    const QString finalPath = m_tempDir.path() + QStringLiteral("/") + first->relativePath;
    QVERIFY(QFileInfo::exists(finalPath));

    // Overwrite the existing file with different content/pixels (simulating corruption / tampering).
    const QImage tampered = solidLayer(4, 2, 50, 60, 70, 255);
    QVERIFY(tampered.save(finalPath, "PNG"));
    const QByteArray tamperedBytes = readAllBytes(finalPath);
    QVERIFY(!tamperedBytes.isEmpty());

    // Committing original again must detect the content mismatch, fail, and keep the tampered bytes.
    CommitDerivedLayerRequest request;
    request.projectId = m_projectId;
    request.projectRoot = m_tempDir.path();
    request.pageId = m_pageId;
    request.layerKind = DerivedLayerKind::Clear;
    request.layer = original;
    auto result = await(m_store->commitDerivedLayer(std::move(request), {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("clear.layer_commit_failed"));

    QCOMPARE(readAllBytes(finalPath), tamperedBytes);
}

#include "moc_FileSystemDerivedAssetStoreTest.cpp"
