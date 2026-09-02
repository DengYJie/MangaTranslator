#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include "FakeDerivedAssetStore.h"
#include "FakePageImageLoader.h"
#include "application/error/ApplicationError.h"
#include "domain/model/ComicProject.h"
#include "rendering/TextLayoutEngine.h"
#include "rendering/TextRasterizer.h"

#include <QColorSpace>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QImage>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace manga;
using namespace manga::ui::screen::editor;

namespace {

template <typename T> [[nodiscard]] bool awaitFuture(const QFuture<T>& future, int timeoutMs = 5000) {
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

QImage makeSolidImage(int w, int h, const QColor& color) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.setColorSpace(QColorSpace(QColorSpace::SRgb));
    img.fill(color);
    return img;
}

} // namespace

class ExportSnapshotBuilderTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void allPagesInProjectOrder();
    void explicitPageIdSubsetPreservesProjectOrder();
    void outputStemAndLayerNullSemantics();
    void missingSourceImageReturnsPageError();
    void emptyPageProducesEmptyTransparentLayerNotFailure();
    void translationLayerMatchesTextRasterizerPixelForPixel();
};

void ExportSnapshotBuilderTest::allPagesInProjectOrder() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    const auto p2 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{100, 100}, 0);
    domain::ComicPage page2(p2, "assets/original/p2.png", domain::ImageSize{100, 100}, 1);
    QVERIFY(project.addPage(page1));
    QVERIFY(project.addPage(page2));

    application::test::FakePageImageLoader loader;
    loader.nextImage = makeSolidImage(100, 100, Qt::white);
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");
    req.pageIds = {};

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(res.has_value());
    const auto& snapshots = res.value();
    QCOMPARE(snapshots.size(), 2);
    QCOMPARE(snapshots[0].pageId, p1);
    QCOMPARE(snapshots[1].pageId, p2);
}

void ExportSnapshotBuilderTest::explicitPageIdSubsetPreservesProjectOrder() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    const auto p2 = domain::PageId::generate();
    const auto p3 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{100, 100}, 0);
    domain::ComicPage page2(p2, "assets/original/p2.png", domain::ImageSize{100, 100}, 1);
    domain::ComicPage page3(p3, "assets/original/p3.png", domain::ImageSize{100, 100}, 2);
    QVERIFY(project.addPage(page1));
    QVERIFY(project.addPage(page2));
    QVERIFY(project.addPage(page3));

    application::test::FakePageImageLoader loader;
    loader.nextImage = makeSolidImage(100, 100, Qt::white);
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");
    // Request in reverse order p3, p1; should preserve natural project order (p1 then p3).
    req.pageIds = {p3, p1};

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(res.has_value());
    const auto& snapshots = res.value();
    QCOMPARE(snapshots.size(), 2);
    QCOMPARE(snapshots[0].pageId, p1);
    QCOMPARE(snapshots[1].pageId, p3);
}

void ExportSnapshotBuilderTest::outputStemAndLayerNullSemantics() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{100, 100}, 0);
    QVERIFY(project.addPage(page1));

    application::test::FakePageImageLoader loader;
    loader.nextImage = makeSolidImage(100, 100, Qt::white);
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(res.has_value());
    const auto& snapshots = res.value();
    QCOMPARE(snapshots.size(), 1);
    const auto& snap = snapshots[0];
    QCOMPARE(snap.pageId, p1);
    QVERIFY(snap.clearLayer.isNull());
    QVERIFY(snap.manualOverrideLayer.isNull());
    QVERIFY(snap.translationLayer.isNull());
}

void ExportSnapshotBuilderTest::missingSourceImageReturnsPageError() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{100, 100}, 0);
    QVERIFY(project.addPage(page1));

    application::test::FakePageImageLoader loader;
    loader.shouldFail = true;
    loader.nextError = application::makeApplicationError(
        application::ErrorCategory::Persistence, QStringLiteral("io.file_not_found"), QStringLiteral("File missing"));
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().category, application::ErrorCategory::Persistence);
}

void ExportSnapshotBuilderTest::emptyPageProducesEmptyTransparentLayerNotFailure() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{200, 300}, 0);
    QVERIFY(project.addPage(page1));

    application::test::FakePageImageLoader loader;
    loader.nextImage = makeSolidImage(200, 300, Qt::white);
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(res.has_value());
    const auto& snapshots = res.value();
    QCOMPARE(snapshots.size(), 1);
    QVERIFY(snapshots[0].translationLayer.isNull() || snapshots[0].translationLayer.size() == QSize(200, 300));
}

void ExportSnapshotBuilderTest::translationLayerMatchesTextRasterizerPixelForPixel() {
    domain::ComicProject project("TestProject");
    const auto p1 = domain::PageId::generate();
    domain::ComicPage page1(p1, "assets/original/p1.png", domain::ImageSize{300, 300}, 0);

    domain::RectGeometry geom{20.0, 20.0, 200.0, 80.0};
    domain::TextRegion region(domain::RegionId::generate(), geom, 0);
    domain::TranslationEntry trans;
    trans.setManualText(QStringLiteral("Hello World").toStdString());
    region.setTranslation(trans);

    domain::TextLayout layout;
    layout.orientation = domain::TextOrientation::Horizontal;
    layout.fontSizePt = 16.0;
    region.setLayout(layout);

    QVERIFY(page1.addRegion(region));
    QVERIFY(project.addPage(page1));

    application::test::FakePageImageLoader loader;
    loader.nextImage = makeSolidImage(300, 300, Qt::white);
    application::test::FakeDerivedAssetStore derivedStore;

    ExportSnapshotBuilder builder(&project, &loader, &derivedStore);
    ExportSnapshotBuilder::Request req;
    req.projectRoot = QStringLiteral("D:/fake/project");
    req.logicalDpi = 96.0;

    application::OperationContext ctx;
    auto future = builder.execute(req, ctx);
    QVERIFY(awaitFuture(future));
    auto res = future.result();
    QVERIFY(res.has_value());
    const auto& snapshots = res.value();
    QCOMPARE(snapshots.size(), 1);

    const auto& translationLayer = snapshots[0].translationLayer;
    QVERIFY(!translationLayer.isNull());
    QCOMPARE(translationLayer.size(), QSize(300, 300));
    QCOMPARE(translationLayer.format(), QImage::Format_RGBA8888);

    // Compute direct layout and rasterization
    const auto expectedLayout =
        rendering::TextLayoutEngine::layoutText(QStringLiteral("Hello World"), geom, layout, 96.0);
    const QImage expectedImage = rendering::TextRasterizer::renderToImage(expectedLayout, 300, 300, 96.0)
                                     .convertToFormat(QImage::Format_RGBA8888);

    // Pixel for pixel equality (tolerance 0)
    QCOMPARE(translationLayer.width(), expectedImage.width());
    QCOMPARE(translationLayer.height(), expectedImage.height());
    for (int y = 0; y < 300; ++y) {
        const auto* actualScan = reinterpret_cast<const quint32*>(translationLayer.constScanLine(y));
        const auto* expectedScan = reinterpret_cast<const quint32*>(expectedImage.constScanLine(y));
        for (int x = 0; x < 300; ++x) {
            QCOMPARE(actualScan[x], expectedScan[x]);
        }
    }
}

QTEST_MAIN(ExportSnapshotBuilderTest)
#include "ExportSnapshotBuilderTest.moc"
