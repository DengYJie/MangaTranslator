#include "FileSystemComicExporterTest.h"

#include "application/dto/ExportDto.h"
#include "data/export/FileSystemComicExporter.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <chrono>
#include <cmath>

using namespace manga;
using namespace manga::application;
using namespace manga::data;

namespace {

void setRgba8888Pixel(QImage& img, int x, int y, quint8 r, quint8 g, quint8 b, quint8 a) {
    auto* scan = img.scanLine(y);
    scan[x * 4 + 0] = r;
    scan[x * 4 + 1] = g;
    scan[x * 4 + 2] = b;
    scan[x * 4 + 3] = a;
}

QImage createSolidImage(int width, int height, quint8 r, quint8 g, quint8 b, quint8 a = 255) {
    QImage img(width, height, QImage::Format_RGBA8888);
    img.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            setRgba8888Pixel(img, x, y, r, g, b, a);
        }
    }
    return img;
}

QImage createTransparentImage(int width, int height) {
    QImage img(width, height, QImage::Format_RGBA8888);
    img.setColorSpace(QColorSpace(QColorSpace::SRgb));
    img.fill(Qt::transparent);
    return img;
}

[[nodiscard]] QString calculateFileHash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

void assertNoExportResidue(const QDir& dir, const char* tag = "") {
    const QStringList files = dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const auto& file : files) {
        QVERIFY2(!file.endsWith(QStringLiteral(".tmp")),
                 qPrintable(QStringLiteral("Residual temporary file after %1: %2").arg(QLatin1String(tag), file)));
        QVERIFY2(!file.endsWith(QStringLiteral(".lock")),
                 qPrintable(QStringLiteral("Residual lock file after %1: %2").arg(QLatin1String(tag), file)));
    }
}

template <typename T> Result<T> await(QFuture<Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

} // namespace

void FileSystemComicExporterTest::initTestCase() {}

void FileSystemComicExporterTest::cleanupTestCase() {}

void FileSystemComicExporterTest::goldenFourLayerSemiTransparentCompositeMatchesAlphaBlendingFormula() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // 2x2 Base image: solid blue RGBA(0, 0, 255, 255)
    QImage base = createSolidImage(2, 2, 0, 0, 255, 255);

    // Layer 1 (Clear): semi-transparent white RGBA(255, 255, 255, 128) at (0, 0)
    QImage clear = createTransparentImage(2, 2);
    setRgba8888Pixel(clear, 0, 0, 255, 255, 255, 128);

    // Layer 2 (Translation): semi-transparent green RGBA(0, 255, 0, 128) at (0, 0)
    QImage translation = createTransparentImage(2, 2);
    setRgba8888Pixel(translation, 0, 0, 0, 255, 0, 128);

    // Layer 3 (ManualOverride): semi-transparent red RGBA(255, 0, 0, 128) at (0, 0)
    QImage manual = createTransparentImage(2, 2);
    setRgba8888Pixel(manual, 0, 0, 255, 0, 0, 128);

    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("golden_page_0001");
    snapshot.sourceImage = base;
    snapshot.clearLayer = clear;
    snapshot.translationLayer = translation;
    snapshot.manualOverrideLayer = manual;

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto result = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(result.has_value());
    QCOMPARE(result->exported.size(), 1);
    QCOMPARE(result->failures.size(), 0);

    const QString exportedPath = result->exported[0].path;
    QVERIFY(QFileInfo::exists(exportedPath));

    QImageReader reader(exportedPath);
    const QImage exportedImage = reader.read();
    QVERIFY(!exportedImage.isNull());
    QCOMPARE(exportedImage.size(), QSize(2, 2));

    // Golden value for pixel (0, 0): Porter-Duff SourceOver over four layers in strict sequence.
    // Base: (0, 0, 255) at alpha 255.
    // Clear composites (255, 255, 255) at alpha 128 to (128, 128, 255, 255).
    // Translation composites (0, 255, 0) at alpha 128 to (64, 192, 127, 255).
    // ManualOverride composites (255, 0, 0) at alpha 128 to (160, 96, 63, 255).
    const QColor c00 = exportedImage.pixelColor(0, 0);
    QCOMPARE(c00.alpha(), 255);
    QVERIFY(std::abs(c00.red() - 160) <= 2);
    QVERIFY(std::abs(c00.green() - 96) <= 2);
    QVERIFY(std::abs(c00.blue() - 63) <= 2);

    // Every other pixel: no layer coverage, so the base (solid blue) must pass through untouched.
    QCOMPARE(exportedImage.pixelColor(1, 0), QColor(0, 0, 255, 255));
    QCOMPARE(exportedImage.pixelColor(0, 1), QColor(0, 0, 255, 255));
    QCOMPARE(exportedImage.pixelColor(1, 1), QColor(0, 0, 255, 255));
    assertNoExportResidue(QDir(tempDir.path()), "golden composite");
}

void FileSystemComicExporterTest::jpegExportFlattensOverOpaqueWhiteAndExcludesMetadata() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Semi-transparent black over transparent background
    QImage base = createSolidImage(4, 4, 0, 0, 0, 128);

    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("page_jpeg");
    snapshot.sourceImage = base;

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Jpeg;
    request.jpegQuality = 90;
    request.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto result = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(result.has_value());
    QCOMPARE(result->exported.size(), 1);
    const QString path = result->exported[0].path;
    QVERIFY(path.endsWith(QStringLiteral(".jpg")));

    QImageReader reader(path);
    const QImage img = reader.read();
    QVERIFY(!img.isNull());
    QCOMPARE(img.size(), QSize(4, 4));

    // JPEG output must have no alpha channel at all (not merely a forced alpha value of 255).
    QVERIFY(!img.hasAlphaChannel());

    // Semi-transparent black on white results in grey with full opacity (alpha = 255)
    const QColor col = img.pixelColor(0, 0);
    QCOMPARE(col.alpha(), 255);
    QVERIFY(col.red() > 100 && col.red() < 150);
    QVERIFY(col.green() > 100 && col.green() < 150);
    QVERIFY(col.blue() > 100 && col.blue() < 150);

    // A JPEG encoder may drop the embedded profile, so an absent or unspecified space also passes.
    QVERIFY(!img.colorSpace().isValid() || img.colorSpace() == QColorSpace(QColorSpace::SRgb) ||
            img.colorSpace().primaries() == QColorSpace::Primaries::SRgb);

    QVERIFY(img.textKeys().isEmpty());
    QVERIFY(reader.textKeys().isEmpty());

    assertNoExportResidue(QDir(tempDir.path()), "jpeg flatten");
}

void FileSystemComicExporterTest::neverOverwriteAllocatesIncrementedFilenamesAndPreservesExistingHashes() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());

    // Pre-create page_0001.png with unique content
    const QString path0 = dir.filePath(QStringLiteral("page_0001.png"));
    QImage img0 = createSolidImage(2, 2, 11, 22, 33, 255);
    QVERIFY(img0.save(path0, "PNG"));
    const QString hash0 = calculateFileHash(path0);
    QVERIFY(!hash0.isEmpty());

    // Pre-create page_0001_001.png with unique content
    const QString path1 = dir.filePath(QStringLiteral("page_0001_001.png"));
    QImage img1 = createSolidImage(2, 2, 44, 55, 66, 255);
    QVERIFY(img1.save(path1, "PNG"));
    const QString hash1 = calculateFileHash(path1);
    QVERIFY(!hash1.isEmpty());

    // New export request for page_0001
    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("page_0001");
    snapshot.sourceImage = createSolidImage(2, 2, 77, 88, 99, 255);

    ExportRequest req;
    req.outputDir = tempDir.path();
    req.format = ExportFormat::Png;
    req.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(req, {}));
    QVERIFY(res.has_value());
    QCOMPARE(res->exported.size(), 1);

    // Exporter must allocate page_0001_002.png
    const QString allocatedPath = res->exported[0].path;
    QVERIFY(allocatedPath.endsWith(QStringLiteral("page_0001_002.png")));
    QVERIFY(QFileInfo::exists(allocatedPath));

    QCOMPARE(calculateFileHash(path0), hash0);
    QCOMPARE(calculateFileHash(path1), hash1);
    assertNoExportResidue(dir, "never overwrite");
}

void FileSystemComicExporterTest::candidateExhaustionReturnsNameExhausted() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());

    // Pre-create all 11 candidate files (base + _001 to _010)
    QFile f0(dir.filePath(QStringLiteral("exhaust_0001.png")));
    QVERIFY(f0.open(QIODevice::WriteOnly));
    f0.close();

    for (int i = 1; i <= 10; ++i) {
        QFile f(dir.filePath(QStringLiteral("exhaust_0001_%1.png").arg(i, 3, 10, QLatin1Char('0'))));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
    }

    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("exhaust_0001");
    snapshot.sourceImage = createSolidImage(2, 2, 1, 2, 3, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(res.has_value());
    QCOMPARE(res->exported.size(), 0);
    QCOMPARE(res->failures.size(), 1);
    QCOMPARE(res->failures[0].error.code, QStringLiteral("export.name_exhausted"));
    assertNoExportResidue(QDir(tempDir.path()), "name exhausted");
}

void FileSystemComicExporterTest::lockedDirectoryReturnsOutputLocked() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Acquire lock on the directory
    QLockFile externalLock(QDir(tempDir.path()).filePath(QStringLiteral(".mangatranslator-export.lock")));
    QVERIFY(externalLock.lock());

    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("locked_page");
    snapshot.sourceImage = createSolidImage(2, 2, 1, 2, 3, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(res.has_value());
    QCOMPARE(res->exported.size(), 0);
    QCOMPARE(res->failures.size(), 1);
    QCOMPARE(res->failures[0].error.code, QStringLiteral("export.output_locked"));

    externalLock.unlock();
    assertNoExportResidue(QDir(tempDir.path()), "output locked");
}

void FileSystemComicExporterTest::invalidLayerGeometryOrFormatFailsWithoutAffectingOtherPages() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Page 1: Invalid layer (dimensions 3x3 does not match base 4x4)
    ExportPageSnapshot page1;
    page1.pageId = domain::PageId::generate();
    page1.outputStem = QStringLiteral("invalid_size_page");
    page1.sourceImage = createSolidImage(4, 4, 10, 10, 10, 255);
    page1.clearLayer = createSolidImage(3, 3, 20, 20, 20, 255);

    // Page 2: Invalid layer (non-RGBA8888 pixel format)
    ExportPageSnapshot page2;
    page2.pageId = domain::PageId::generate();
    page2.outputStem = QStringLiteral("invalid_format_page");
    page2.sourceImage = createSolidImage(4, 4, 10, 10, 10, 255);
    QImage rgb888(4, 4, QImage::Format_RGB888);
    page2.clearLayer = rgb888;

    // Page 3: Invalid layer (explicit non-sRGB color space)
    ExportPageSnapshot page3;
    page3.pageId = domain::PageId::generate();
    page3.outputStem = QStringLiteral("invalid_colorspace_page");
    page3.sourceImage = createSolidImage(4, 4, 10, 10, 10, 255);
    QImage wideGamut(4, 4, QImage::Format_RGBA8888);
    wideGamut.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb));
    page3.translationLayer = wideGamut;

    // Page 4: Valid page
    ExportPageSnapshot page4;
    page4.pageId = domain::PageId::generate();
    page4.outputStem = QStringLiteral("valid_page");
    page4.sourceImage = createSolidImage(4, 4, 30, 30, 30, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {page1, page2, page3, page4};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(res.has_value());
    // Page 4 (the only valid page) succeeded
    QCOMPARE(res->exported.size(), 1);
    QCOMPARE(res->exported[0].pageId, page4.pageId);
    // Pages 1-3 failed with export.invalid_layer, one failure per page
    QCOMPARE(res->failures.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(res->failures[i].error.code, QStringLiteral("export.invalid_layer"));
    }
    QCOMPARE(res->failures[0].pageId, page1.pageId);
    QCOMPARE(res->failures[1].pageId, page2.pageId);
    QCOMPARE(res->failures[2].pageId, page3.pageId);
    assertNoExportResidue(QDir(tempDir.path()), "invalid layer");
}

void FileSystemComicExporterTest::noTemporaryOrLockFilesRemainAfterExport() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());

    ExportPageSnapshot page1;
    page1.pageId = domain::PageId::generate();
    page1.outputStem = QStringLiteral("clean_page");
    page1.sourceImage = createSolidImage(2, 2, 1, 1, 1, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {page1};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), {}));
    QVERIFY(res.has_value() && res->exported.size() == 1);

    const QStringList files = dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const auto& file : files) {
        QVERIFY(!file.endsWith(QStringLiteral(".tmp")));
        QVERIFY(!file.endsWith(QStringLiteral(".lock")));
    }
}

void FileSystemComicExporterTest::stopRequestedBeforeStartCancelsExport() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    std::stop_source source;
    source.request_stop();
    OperationContext context;
    context.stopToken = source.get_token();

    ExportPageSnapshot page1;
    page1.pageId = domain::PageId::generate();
    page1.outputStem = QStringLiteral("cancelled_page_1");
    page1.sourceImage = createSolidImage(2, 2, 1, 1, 1, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {page1};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), context));

    // When cancelled before start, returns top-level cancelled Result
    QVERIFY(!res.has_value());
    QVERIFY(res.error().isCancelled());
    assertNoExportResidue(QDir(tempDir.path()), "cancel before start");
}

void FileSystemComicExporterTest::invalidOutputStemIsRejected() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ExportPageSnapshot snapshot;
    snapshot.pageId = domain::PageId::generate();
    snapshot.outputStem = QStringLiteral("../escaped_stem");
    snapshot.sourceImage = createSolidImage(2, 2, 1, 1, 1, 255);

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    request.pages = {snapshot};

    FileSystemComicExporter exporter;
    auto res = await(exporter.exportPages(std::move(request), {}));

    QVERIFY(res.has_value());
    QCOMPARE(res->exported.size(), 0);
    QCOMPARE(res->failures.size(), 1);
    QCOMPARE(res->failures[0].error.code, QStringLiteral("export.invalid_output_name"));
    assertNoExportResidue(QDir(tempDir.path()), "invalid stem");
}

void FileSystemComicExporterTest::stopAfterFirstPageReturnsPartialSuccess() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir dir(tempDir.path());

    std::stop_source source;
    OperationContext context;
    context.stopToken = source.get_token();

    ExportRequest request;
    request.outputDir = tempDir.path();
    request.format = ExportFormat::Png;
    QVector<domain::PageId> pageIds;
    for (int i = 0; i < 4; ++i) {
        ExportPageSnapshot page;
        page.pageId = domain::PageId::generate();
        pageIds.append(page.pageId);
        page.outputStem = QStringLiteral("partial_%1").arg(i, 4, 10, QLatin1Char('0'));
        page.sourceImage = createSolidImage(256, 256, 0, quint8(i * 60 % 256), 128, 255);
        request.pages.append(page);
    }

    FileSystemComicExporter exporter;
    auto future = exporter.exportPages(request, context);

    // Cancellation is cooperative. Another page may finish between observing the first file
    // and the worker's next stop check, so assert the committed prefix and cancelled suffix.
    const QString firstFile = dir.filePath(QStringLiteral("partial_0000.png"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!QFileInfo::exists(firstFile)) {
        if (std::chrono::steady_clock::now() > deadline) {
            QFAIL("Timed out waiting for the first exported page");
        }
        QTest::qWait(5);
    }
    source.request_stop();
    future.waitForFinished();

    const auto result = future.result();
    QVERIFY(result.has_value());
    QVERIFY(result->exported.size() >= 1);
    QVERIFY(result->exported.size() < pageIds.size());
    QCOMPARE(result->failures.size(), pageIds.size() - result->exported.size());
    for (int i = 0; i < result->exported.size(); ++i) {
        QCOMPARE(result->exported[i].pageId, pageIds[i]);
    }
    for (int i = 0; i < result->failures.size(); ++i) {
        const int pageIndex = result->exported.size() + i;
        QCOMPARE(result->failures[i].pageId, pageIds[pageIndex]);
        QVERIFY(result->failures[i].error.isCancelled());
        QVERIFY(
            !QFileInfo::exists(dir.filePath(QStringLiteral("partial_%1.png").arg(pageIndex, 4, 10, QLatin1Char('0')))));
    }
    assertNoExportResidue(dir, "partial cancel");
}

void FileSystemComicExporterTest::jpegQualityRangeAffectsFileSize() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Pseudo-random noise keeps the quality setting observable: low quality quantizes away the
    // high-frequency detail while high quality preserves it, so the size gap is large and stable.
    QImage noiseImage(256, 256, QImage::Format_RGBA8888);
    noiseImage.setColorSpace(QColorSpace(QColorSpace::SRgb));
    quint32 lcg = 0x12345678u;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            lcg = lcg * 1664525u + 1013904223u;
            setRgba8888Pixel(noiseImage, x, y, quint8(lcg >> 24), quint8(lcg >> 16), quint8(lcg >> 8), 255);
        }
    }

    auto exportWithQuality = [&](int quality) -> qint64 {
        ExportPageSnapshot snapshot;
        snapshot.pageId = domain::PageId::generate();
        snapshot.outputStem = QStringLiteral("quality_%1").arg(quality);
        snapshot.sourceImage = noiseImage;
        ExportRequest request;
        request.outputDir = tempDir.path();
        request.format = ExportFormat::Jpeg;
        request.jpegQuality = quality;
        request.pages = {snapshot};
        FileSystemComicExporter exporter;
        auto res = await(exporter.exportPages(std::move(request), {}));
        if (!res.has_value()) {
            qFatal("Export with quality %d failed: %s", quality, qPrintable(res.error().code));
        }
        if (res->exported.size() != 1) {
            qFatal("Export with quality %d produced %lld pages instead of 1",
                   quality,
                   static_cast<long long>(res->exported.size()));
        }
        return QFileInfo(res->exported[0].path).size();
    };

    const qint64 low = exportWithQuality(1);
    const qint64 high = exportWithQuality(95);
    QVERIFY2(high > low * 2,
             qPrintable(QStringLiteral("quality=95 (%1 bytes) must be substantially larger than quality=1 (%2 bytes)")
                            .arg(high)
                            .arg(low)));
    assertNoExportResidue(QDir(tempDir.path()), "jpeg quality");
}

#include "moc_FileSystemComicExporterTest.cpp"
