#include "FileSystemComicImporterTest.h"

#include "application/dto/ImportDto.h"
#include "application/operation/OperationContext.h"
#include "data/import/FileSystemComicImporter.h"
#include "data/import/FileSystemPageImageLoader.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "domain/model/ComicPage.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QImageReader>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>

using namespace manga;
using namespace manga::data;
using domain::PageId;

namespace {

template <typename T> application::Result<T> await(QFuture<application::Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

void expectNoError(const application::ApplicationError& error) {
    if (!error.code.isEmpty()) {
        QFAIL(qPrintable(QStringLiteral("unexpected error: %1").arg(error.code)));
    }
}

void expectStdString(const std::string& actual, const QString& expected) {
    QCOMPARE(QString::fromStdString(actual), expected);
}

QVector<QString> pathsOf(const application::ImportImagesResult& result) {
    QVector<QString> paths;
    for (const auto& imported : result.importedPages) {
        paths.append(imported.sourcePath);
    }
    return paths;
}

} // namespace

void FileSystemComicImporterTest::init() {
    QVERIFY(m_tempDir.isValid());
    QVERIFY(materializeFixtures());
    m_coordinator = std::make_unique<ProjectIoCoordinator>();
    m_repository = std::make_unique<FileSystemProjectRepository>(m_coordinator.get());
    m_importer = std::make_unique<FileSystemComicImporter>(m_coordinator.get());
    m_loader = std::make_unique<FileSystemPageImageLoader>(m_coordinator.get());
    QVERIFY(createTestProject());
}

void FileSystemComicImporterTest::cleanup() {
    m_loader.reset();
    m_importer.reset();
    m_repository.reset();
    m_coordinator.reset();
}

// Copies the embedded fixtures into a real directory: production always imports from real
// filesystem paths, and Qt path helpers mangle ":/" resource paths on Windows.
bool FileSystemComicImporterTest::materializeFixtures() {
    const QString sourcesDir = m_tempDir.path() + QStringLiteral("/sources");
    if (!QDir().mkpath(sourcesDir) || !QDir().mkpath(sourcesDir + QStringLiteral("/pages"))) {
        return false;
    }
    const auto copyAll = [](const QString& from, const QString& to) {
        for (const QString& entry : QDir(from).entryList(QDir::Files)) {
            const QString target = to + QLatin1Char('/') + entry;
            if (!QFileInfo::exists(target) && !QFile::copy(from + QLatin1Char('/') + entry, target)) {
                return false;
            }
        }
        return true;
    };
    return copyAll(QStringLiteral(":/fixtures"), sourcesDir) &&
           copyAll(QStringLiteral(":/fixtures/pages"), sourcesDir + QStringLiteral("/pages"));
}

bool FileSystemComicImporterTest::createTestProject() {
    application::CreateProjectRequest request;
    request.name = QStringLiteral("Import Test");
    request.path = m_tempDir.path() + QStringLiteral("/project-%1").arg(m_requestCounter++);
    request.sourceLanguage = domain::languages::Japanese;
    request.targetLanguage = domain::languages::SimplifiedChinese;
    request.readingDirection = domain::ReadingDirection::RightToLeft;
    auto result = await(m_repository->createProject(std::move(request), {}));
    if (!result) {
        qWarning() << "createProject failed:" << result.error().code;
        return false;
    }
    m_projectDescriptor = *result;
    m_projectRoot = result->path;
    return true;
}

QString FileSystemComicImporterTest::fixture(const QString& name) const {
    return m_tempDir.path() + QStringLiteral("/sources/") + name;
}

std::optional<application::ImportImagesResult> FileSystemComicImporterTest::import(const QVector<QString>& paths) {
    application::ImportImagesRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.filePaths = paths;
    auto result = await(m_importer->importImages(std::move(request), {}));
    if (!result) {
        qWarning() << "importImages failed:" << result.error().code;
        return std::nullopt;
    }
    for (const auto& failure : result->failures) {
        qWarning() << "import failure:" << failure.path << failure.error.code << failure.error.diagnostic;
    }
    return *result;
}

application::ApplicationError FileSystemComicImporterTest::importError(const QVector<QString>& paths) {
    application::ImportImagesRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.filePaths = paths;
    auto result = await(m_importer->importImages(std::move(request), {}));
    if (result) {
        return {};
    }
    return result.error();
}

std::optional<QImage> FileSystemComicImporterTest::loadPageImage(const QString& assetPath,
                                                                 domain::ImageSize expectedSize) {
    application::LoadPageImageRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.pageId = PageId::generate();
    request.assetPath = assetPath;
    request.expectedSize = expectedSize;
    auto result = await(m_loader->loadPageImage(std::move(request), {}));
    if (!result) {
        qWarning() << "loadPageImage failed:" << result.error().code;
        return std::nullopt;
    }
    return *result;
}

void FileSystemComicImporterTest::webpPluginIsAvailable() {
    QVERIFY(QImageReader::supportedImageFormats().contains("webp"));
}

void FileSystemComicImporterTest::importSingleImage() {
    const auto result = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(result.has_value());
    QVERIFY(result->importedPages.size() == 1);
    QVERIFY(result->failures.isEmpty());
    expectStdString(result->importedPages.front().page.originalAssetPath(),
                    QStringLiteral("assets/original/page_0001.png"));
    QVERIFY((result->importedPages.front().page.imageSize() == domain::ImageSize{3, 2}));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0001.png")));
    QVERIFY(result->importedPages.front().warnings.isEmpty());
}

void FileSystemComicImporterTest::importFolderNaturalOrder() {
    const auto result = import({fixture(QStringLiteral("pages"))});
    QVERIFY(result.has_value());
    QCOMPARE(result->importedPages.size(), 3);
    // Natural order: page1, page2, page10 (numeric, not lexicographic).
    QVERIFY(pathsOf(*result)[0].endsWith(QStringLiteral("page1.png")));
    QVERIFY(pathsOf(*result)[1].endsWith(QStringLiteral("page2.png")));
    QVERIFY(pathsOf(*result)[2].endsWith(QStringLiteral("page10.png")));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0003.png")));
}

void FileSystemComicImporterTest::duplicateViaFolderAndDirectRejected() {
    const QString folder = fixture(QStringLiteral("pages"));
    const QString direct = fixture(QStringLiteral("pages/page1.png"));
    QCOMPARE(importError({folder, direct}).code, QStringLiteral("input.duplicate_import_path"));
    QCOMPARE(
        QDir(m_projectRoot + QStringLiteral("/assets/original")).entryList(QDir::Files | QDir::NoDotAndDotDot).size(),
        0);
}

void FileSystemComicImporterTest::reimportCreatesNewAsset() {
    auto first = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(first.has_value() && first->importedPages.size() == 1);
    auto second = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(second.has_value() && second->importedPages.size() == 1);
    QVERIFY(second->importedPages.front().page.id() != first->importedPages.front().page.id());
    expectStdString(second->importedPages.front().page.originalAssetPath(),
                    QStringLiteral("assets/original/page_0002.png"));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0001.png")));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0002.png")));
}

void FileSystemComicImporterTest::disguisedExtensionWarns() {
    const auto result = import({fixture(QStringLiteral("disguised.jpg"))});
    QVERIFY(result.has_value() && result->importedPages.size() == 1);
    // PNG content is committed with the canonical .png suffix regardless of the source name.
    expectStdString(result->importedPages.front().page.originalAssetPath(),
                    QStringLiteral("assets/original/page_0001.png"));
    const bool mismatchWarning = std::any_of(result->importedPages.front().warnings.cbegin(),
                                             result->importedPages.front().warnings.cend(),
                                             [](const application::ImportWarning& warning) {
                                                 return warning.code == QStringLiteral("import.extension_mismatch");
                                             });
    QVERIFY(mismatchWarning);
}

void FileSystemComicImporterTest::nonImageContentFails() {
    const auto result = import({fixture(QStringLiteral("not-an-image.png"))});
    QVERIFY(result.has_value());
    QVERIFY(result->importedPages.isEmpty());
    QCOMPARE(result->failures.size(), 1);
    QCOMPARE(result->failures.front().error.code, QStringLiteral("import.unsupported_format"));
    QCOMPARE(
        QDir(m_projectRoot + QStringLiteral("/assets/original")).entryList(QDir::Files | QDir::NoDotAndDotDot).size(),
        0);
}

void FileSystemComicImporterTest::exifOrientationApplied() {
    const auto result = import({fixture(QStringLiteral("exif-orientation.jpg"))});
    QVERIFY(result.has_value() && result->importedPages.size() == 1);
    // The 2x1 source carries EXIF orientation 6; the normalized work image is 1x2.
    QVERIFY((result->importedPages.front().page.imageSize() == domain::ImageSize{1, 2}));
    const auto image = loadPageImage(QStringLiteral("assets/original/page_0001.jpg"), {1, 2});
    QVERIFY(image.has_value());
    QCOMPARE(image->size(), QSize(1, 2));
}

void FileSystemComicImporterTest::specialFormatsDecode() {
    // 16-bit grayscale PNG.
    auto gray = import({fixture(QStringLiteral("gray16.png"))});
    QVERIFY(gray.has_value() && gray->importedPages.size() == 1);

    // ICC-tagged PNG.
    auto icc = import({fixture(QStringLiteral("icc-profile.png"))});
    QVERIFY(icc.has_value() && icc->importedPages.size() == 1);

    // CMYK JPEG carries a conversion warning.
    auto cmyk = import({fixture(QStringLiteral("cmyk.jpg"))});
    QVERIFY(cmyk.has_value() && cmyk->importedPages.size() == 1);
    const bool cmykWarning = std::any_of(cmyk->importedPages.front().warnings.cbegin(),
                                         cmyk->importedPages.front().warnings.cend(),
                                         [](const application::ImportWarning& warning) {
                                             return warning.code == QStringLiteral("import.cmyk_converted");
                                         });
    QVERIFY(cmykWarning);

    // Animated WEBP keeps only the first frame and warns.
    auto animated = import({fixture(QStringLiteral("animated.webp"))});
    QVERIFY(animated.has_value() && animated->importedPages.size() == 1);
    const bool animatedWarning =
        std::any_of(animated->importedPages.front().warnings.cbegin(),
                    animated->importedPages.front().warnings.cend(),
                    [](const application::ImportWarning& warning) {
                        return warning.code == QStringLiteral("import.animated_webp_first_frame");
                    });
    QVERIFY(animatedWarning);
}

void FileSystemComicImporterTest::transparentPngKeepsAlpha() {
    const auto result = import({fixture(QStringLiteral("transparent.png"))});
    QVERIFY(result.has_value() && result->importedPages.size() == 1);
    const auto image = loadPageImage(QStringLiteral("assets/original/page_0001.png"), {2, 2});
    QVERIFY(image.has_value());
    QCOMPARE(image->format(), QImage::Format_RGBA8888);
    QCOMPARE(image->colorSpace(), QColorSpace(QColorSpace::SRgb));
    QCOMPARE(image->pixelColor(0, 0).alpha(), 0);
    QCOMPARE(image->pixelColor(1, 1).alpha(), 128);
}

void FileSystemComicImporterTest::oversizeImagesRejected() {
    auto tooWide = import({fixture(QStringLiteral("too-wide.png"))});
    QVERIFY(tooWide.has_value());
    QVERIFY(tooWide->importedPages.isEmpty());
    QCOMPARE(tooWide->failures.size(), 1);
    QCOMPARE(tooWide->failures.front().error.code, QStringLiteral("input.image_too_large"));

    auto tooManyPixels = import({fixture(QStringLiteral("too-many-pixels.png"))});
    QVERIFY(tooManyPixels.has_value());
    QCOMPARE(tooManyPixels->failures.size(), 1);
    QCOMPARE(tooManyPixels->failures.front().error.code, QStringLiteral("input.image_too_large"));
}

void FileSystemComicImporterTest::partialFailureKeepsOrder() {
    const auto result = import({fixture(QStringLiteral("not-an-image.png")), fixture(QStringLiteral("sample.png"))});
    QVERIFY(result.has_value());
    QCOMPARE(result->importedPages.size(), 1);
    QCOMPARE(result->failures.size(), 1);
    QVERIFY(result->importedPages.front().sourcePath.endsWith(QStringLiteral("sample.png")));
    QVERIFY(result->failures.front().path.endsWith(QStringLiteral("not-an-image.png")));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0001.png")));
}

void FileSystemComicImporterTest::cancelledImportWritesNothing() {
    application::OperationContext context;
    std::stop_source source;
    source.request_stop();
    context.stopToken = source.get_token();

    application::ImportImagesRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.filePaths = {fixture(QStringLiteral("sample.png"))};
    auto result = await(m_importer->importImages(std::move(request), context));
    QVERIFY(!result.has_value());
    QVERIFY(result.error().isCancelled());
    QCOMPARE(
        QDir(m_projectRoot + QStringLiteral("/assets/original")).entryList(QDir::Files | QDir::NoDotAndDotDot).size(),
        0);
}

void FileSystemComicImporterTest::cancelledMidImportRollsBackCommittedAssets() {
    // Distinct copies so the request passes the duplicate rejection.
    const QString bulkDir = m_tempDir.path() + QStringLiteral("/sources/bulk");
    QVERIFY(QDir().mkpath(bulkDir));
    QFile sample(fixture(QStringLiteral("sample.png")));
    QVERIFY(sample.open(QIODevice::ReadOnly));
    const QByteArray bytes = sample.readAll();
    sample.close();

    constexpr int kFileCount = 3;
    QVector<QString> paths;
    for (int i = 0; i < kFileCount; ++i) {
        const QString name = QStringLiteral("bulk_%1.png").arg(i, 2, 10, QLatin1Char('0'));
        QFile out(bulkDir + QLatin1Char('/') + name);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        out.close();
        paths.append(bulkDir + QLatin1Char('/') + name);
    }

    std::stop_source stopSource;
    m_importer = std::make_unique<FileSystemComicImporter>(m_coordinator.get(), [&stopSource](int committedCount) {
        if (committedCount == 1) {
            stopSource.request_stop();
        }
    });
    application::OperationContext context;
    context.stopToken = stopSource.get_token();
    application::ImportImagesRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.filePaths = paths;
    auto future = m_importer->importImages(std::move(request), context);
    future.waitForFinished();

    auto result = future.result();
    QVERIFY(!result.has_value());
    QVERIFY(result.error().isCancelled());
    // Every asset committed by this request was rolled back, and the staging tree is gone.
    QCOMPARE(
        QDir(m_projectRoot + QStringLiteral("/assets/original")).entryList(QDir::Files | QDir::NoDotAndDotDot).size(),
        0);
    QVERIFY(!QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/staging")));
}

void FileSystemComicImporterTest::concurrentImportsSerialize() {
    auto first = m_importer->importImages(
        [&] {
            application::ImportImagesRequest request;
            request.projectId = m_projectDescriptor.id;
            request.projectRoot = m_projectRoot;
            request.filePaths = {fixture(QStringLiteral("sample.png"))};
            return request;
        }(),
        {});
    auto second = m_importer->importImages(
        [&] {
            application::ImportImagesRequest request;
            request.projectId = m_projectDescriptor.id;
            request.projectRoot = m_projectRoot;
            request.filePaths = {fixture(QStringLiteral("sample.jpg"))};
            return request;
        }(),
        {});
    first.waitForFinished();
    second.waitForFinished();
    QVERIFY(first.result().has_value());
    QVERIFY(second.result().has_value());
    // The coordinator serializes the requests, so asset numbers never collide.
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0001.png")));
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/assets/original/page_0002.jpg")));
}

void FileSystemComicImporterTest::discardRemovesAssets() {
    auto imported = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(imported.has_value() && imported->importedPages.size() == 1);
    const QString assetPath = QString::fromStdString(imported->importedPages.front().page.originalAssetPath());
    QVERIFY(QFileInfo::exists(m_projectRoot + QStringLiteral("/") + assetPath));

    application::DiscardImportedAssetsRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.batchId = imported->batchId;
    request.assetPaths = {assetPath};
    expectNoError([&] {
        auto result = await(m_importer->discardImportedAssets(request, {}));
        return result ? application::ApplicationError{} : result.error();
    }());
    QVERIFY(!QFileInfo::exists(m_projectRoot + QStringLiteral("/") + assetPath));

    // A consumed batch capability cannot be reused, and paths outside assets/original fail.
    auto secondDiscard = await(m_importer->discardImportedAssets(request, {}));
    QVERIFY(!secondDiscard.has_value());
    QCOMPARE(secondDiscard.error().code, QStringLiteral("input.invalid_import_cleanup"));

    application::DiscardImportedAssetsRequest escapeRequest;
    escapeRequest.projectId = m_projectDescriptor.id;
    escapeRequest.projectRoot = m_projectRoot;
    escapeRequest.batchId = application::OperationId::generate();
    escapeRequest.assetPaths = {QStringLiteral("assets/derived/evil.png")};
    auto rejected = await(m_importer->discardImportedAssets(escapeRequest, {}));
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, QStringLiteral("input.invalid_import_cleanup"));
}

void FileSystemComicImporterTest::acknowledgedBatchCannotDeleteAdoptedAsset() {
    auto imported = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(imported.has_value() && imported->importedPages.size() == 1);
    const QString assetPath = QString::fromStdString(imported->importedPages.front().page.originalAssetPath());

    application::AcknowledgeImportedAssetsRequest acknowledge;
    acknowledge.projectId = m_projectDescriptor.id;
    acknowledge.projectRoot = m_projectRoot;
    acknowledge.batchId = imported->batchId;
    QVERIFY(await(m_importer->acknowledgeImportedAssets(acknowledge, {})).has_value());

    application::DiscardImportedAssetsRequest discard;
    discard.projectId = m_projectDescriptor.id;
    discard.projectRoot = m_projectRoot;
    discard.batchId = imported->batchId;
    discard.assetPaths = {assetPath};
    const auto result = await(m_importer->discardImportedAssets(discard, {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_import_cleanup"));
    QVERIFY(QFileInfo::exists(m_projectRoot + QLatin1Char('/') + assetPath));
}

void FileSystemComicImporterTest::projectSessionBindingRejectsMismatchedAndClosedRequests() {
    application::ImportImagesRequest mismatched;
    mismatched.projectId = domain::ProjectId::generate();
    mismatched.projectRoot = m_projectRoot;
    mismatched.filePaths = {fixture(QStringLiteral("sample.png"))};
    auto mismatchResult = await(m_importer->importImages(mismatched, {}));
    QVERIFY(!mismatchResult.has_value());
    QCOMPARE(mismatchResult.error().code, QStringLiteral("project.not_open"));

    application::CloseProjectRequest close;
    close.projectId = m_projectDescriptor.id;
    QVERIFY(await(m_repository->closeProject(close, {})).has_value());

    mismatched.projectId = m_projectDescriptor.id;
    auto closedResult = await(m_importer->importImages(mismatched, {}));
    QVERIFY(!closedResult.has_value());
    QCOMPARE(closedResult.error().code, QStringLiteral("project.not_open"));
}

void FileSystemComicImporterTest::loaderRoundTripMatchesImport() {
    auto imported = import({fixture(QStringLiteral("exif-orientation.jpg"))});
    QVERIFY(imported.has_value() && imported->importedPages.size() == 1);
    const auto& page = imported->importedPages.front().page;

    application::LoadPageImageRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.pageId = page.id();
    request.assetPath = QString::fromStdString(page.originalAssetPath());
    request.expectedSize = page.imageSize();
    auto image = await(m_loader->loadPageImage(std::move(request), {}));
    QVERIFY(image.has_value());
    QCOMPARE(image->format(), QImage::Format_RGBA8888);
    QVERIFY((image->size() == QSize(page.imageSize().width, page.imageSize().height)));
}

void FileSystemComicImporterTest::loaderRejectsMissingAndEscape() {
    application::LoadPageImageRequest missing;
    missing.projectId = m_projectDescriptor.id;
    missing.projectRoot = m_projectRoot;
    missing.pageId = PageId::generate();
    missing.assetPath = QStringLiteral("assets/original/does-not-exist.png");
    missing.expectedSize = {3, 2};
    auto missingResult = await(m_loader->loadPageImage(std::move(missing), {}));
    QVERIFY(!missingResult.has_value());
    QCOMPARE(missingResult.error().code, QStringLiteral("persistence.not_found"));

    application::LoadPageImageRequest escape;
    escape.projectId = m_projectDescriptor.id;
    escape.projectRoot = m_projectRoot;
    escape.pageId = PageId::generate();
    escape.assetPath = QStringLiteral("../outside.png");
    escape.expectedSize = {3, 2};
    auto escapeResult = await(m_loader->loadPageImage(std::move(escape), {}));
    QVERIFY(!escapeResult.has_value());
    QCOMPARE(escapeResult.error().code, QStringLiteral("input.invalid_image_request"));
}

void FileSystemComicImporterTest::loaderRejectsDimensionMismatch() {
    auto imported = import({fixture(QStringLiteral("sample.png"))});
    QVERIFY(imported.has_value() && imported->importedPages.size() == 1);
    const auto& page = imported->importedPages.front().page;

    application::LoadPageImageRequest request;
    request.projectId = m_projectDescriptor.id;
    request.projectRoot = m_projectRoot;
    request.pageId = page.id();
    request.assetPath = QString::fromStdString(page.originalAssetPath());
    request.expectedSize = {page.imageSize().width + 1, page.imageSize().height};
    auto result = await(m_loader->loadPageImage(request, {}));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("project.image_dimension_mismatch"));
}

void FileSystemComicImporterTest::sourceFileUnchangedAfterImportAndSave() {
    const QString source = fixture(QStringLiteral("sample.png"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray sourceHash = QCryptographicHash::hash(sourceFile.readAll(), QCryptographicHash::Sha256);
    sourceFile.close();

    auto imported = import({source});
    QVERIFY(imported.has_value() && imported->importedPages.size() == 1);

    // Save and reopen a project containing the imported page.
    auto opened = await(m_repository->openProject(
        [&] {
            application::OpenProjectRequest request;
            request.path = m_projectRoot;
            return request;
        }(),
        {}));
    QVERIFY(opened.has_value());
    domain::ComicProject& project = opened->project;
    QVERIFY(project.addPage(imported->importedPages.front().page));
    application::SaveProjectRequest saveRequest;
    saveRequest.project = project;
    QVERIFY(await(m_repository->saveProject(std::move(saveRequest), {})).has_value());

    QFile afterFile(source);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(afterFile.readAll(), QCryptographicHash::Sha256), sourceHash);
    afterFile.close();
}

#include "moc_FileSystemComicImporterTest.cpp"
