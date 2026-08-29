#include "SolidColorInpainterTest.h"

#include "domain/model/ClearSettings.h"
#include "services/execution/BoundedTaskExecutor.h"
#include "services/inpaint/SolidColorInpainter.h"

#include <QColorSpace>
#include <QtTest/QtTest>

#include <limits>
#include <memory>

using namespace manga;
using manga::services::BoundedTaskExecutor;
using manga::services::SolidColorInpainter;

namespace {

template <typename T> application::Result<T> await(QFuture<application::Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

application::ClearRequest makeClearRequest(const QImage& page, const domain::RectGeometry& region) {
    application::ClearRequest request;
    request.projectId = domain::ProjectId::generate();
    request.pageId = domain::PageId::generate();
    request.regionId = domain::RegionId::generate();
    request.pageImage = page;
    request.region = region;
    request.settings.colorHex = "#FF8000";
    request.settings.opacity = 0.5;
    return request;
}

} // namespace

void SolidColorInpainterTest::init() {
    m_executor = std::make_unique<BoundedTaskExecutor>(2);
    m_inpainter = std::make_unique<SolidColorInpainter>(m_executor.get());
}

void SolidColorInpainterTest::cleanup() {
    m_inpainter.reset();
    m_executor.reset();
}

void SolidColorInpainterTest::fillProducesExactPixelsAndBounds() {
    QImage page(100, 100, QImage::Format_RGBA8888);
    page.fill(0);
    auto request = makeClearRequest(page, domain::RectGeometry(10.2, 20.7, 30.5, 40.3));

    auto future = m_inpainter->clear(std::move(request), {});
    future.waitForFinished();
    const auto result = future.result();

    QCOMPARE(future.progressMinimum(), 0);
    QCOMPARE(future.progressMaximum(), 1);
    QCOMPARE(future.progressValue(), 1);

    QVERIFY(result.has_value());
    const auto& response = *result;
    // Expansion: left/top floor, right/bottom ceil.
    QVERIFY(response.affectedBounds == (domain::RectGeometry{.x = 10.0, .y = 20.0, .width = 31.0, .height = 41.0}));
    QCOMPARE(response.patch.size(), QSize(31, 41));
    QCOMPARE(response.patch.format(), QImage::Format_RGBA8888);
    QCOMPARE(response.patch.colorSpace(), QColorSpace(QColorSpace::SRgb));
    const QColor pixel = response.patch.pixelColor(0, 0);
    QCOMPARE(pixel.red(), 255);
    QCOMPARE(pixel.green(), 128);
    QCOMPARE(pixel.blue(), 0);
    // opacity 0.5 * 255 rounds to 128.
    QCOMPARE(pixel.alpha(), 128);
}

void SolidColorInpainterTest::clampsRegionToPageBounds() {
    QImage page(100, 100, QImage::Format_RGBA8888);
    page.fill(0);
    auto request = makeClearRequest(page, domain::RectGeometry(-5.5, -5.5, 10, 10));

    auto future = m_inpainter->clear(std::move(request), {});
    future.waitForFinished();

    const auto result = future.result();
    QVERIFY(result.has_value());
    QVERIFY(result->affectedBounds == (domain::RectGeometry{.x = 0.0, .y = 0.0, .width = 5.0, .height = 5.0}));
    QCOMPARE(result->patch.size(), QSize(5, 5));
}

void SolidColorInpainterTest::emptyIntersectionRejected() {
    QImage page(100, 100, QImage::Format_RGBA8888);
    page.fill(0);
    auto request = makeClearRequest(page, domain::RectGeometry(105, 0, 5, 5));

    auto future = m_inpainter->clear(std::move(request), {});
    future.waitForFinished();

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("clear.invalid_region"));
}

void SolidColorInpainterTest::clampedTinyRegionRejected() {
    QImage page(100, 100, QImage::Format_RGBA8888);
    page.fill(0);
    // left clamps to 99, right clamps to 100: a single-pixel column is below 2x2.
    auto request = makeClearRequest(page, domain::RectGeometry(99, 0, 1.0, 5));

    auto future = m_inpainter->clear(std::move(request), {});
    future.waitForFinished();

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("clear.invalid_region"));
}

void SolidColorInpainterTest::nonFiniteRegionRejected() {
    QImage page(10, 10, QImage::Format_RGBA8888);
    page.fill(0);
    auto request = makeClearRequest(page, domain::RectGeometry(0, 0, 4, 4));
    request.region.width = std::numeric_limits<double>::quiet_NaN();

    auto future = m_inpainter->clear(std::move(request), {});
    future.waitForFinished();

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("clear.invalid_region"));
}

void SolidColorInpainterTest::invalidColorRejected() {
    QImage page(10, 10, QImage::Format_RGBA8888);
    page.fill(0);

    for (const char* bad : {"red", "#FFF", "#GGHHII", "FFFFFF", "#FFFFFF "}) {
        auto request = makeClearRequest(page, domain::RectGeometry(0, 0, 4, 4));
        request.settings.colorHex = bad;
        auto future = m_inpainter->clear(std::move(request), {});
        future.waitForFinished();
        QVERIFY2(!future.result().has_value(), qPrintable(QStringLiteral("color '%1' was accepted").arg(bad)));
        QCOMPARE(future.result().error().code, QStringLiteral("clear.invalid_color"));
    }
}

void SolidColorInpainterTest::invalidOpacityRejected() {
    QImage page(10, 10, QImage::Format_RGBA8888);
    page.fill(0);

    for (double bad : {1.5, -0.25, std::numeric_limits<double>::quiet_NaN()}) {
        auto request = makeClearRequest(page, domain::RectGeometry(0, 0, 4, 4));
        request.settings.opacity = bad;
        auto future = m_inpainter->clear(std::move(request), {});
        future.waitForFinished();
        QVERIFY(!future.result().has_value());
        QCOMPARE(future.result().error().code, QStringLiteral("clear.invalid_opacity"));
    }
}

void SolidColorInpainterTest::preCancelledRequestReturnsCancelled() {
    QImage page(10, 10, QImage::Format_RGBA8888);
    page.fill(0);

    std::stop_source source;
    source.request_stop();
    application::OperationContext context;
    context.stopToken = source.get_token();

    auto future = m_inpainter->clear(makeClearRequest(page, domain::RectGeometry(0, 0, 4, 4)), context);
    future.waitForFinished();

    QVERIFY(!future.result().has_value());
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(future.result().error().code, QStringLiteral("cancelled.user_requested"));
}

#include "moc_SolidColorInpainterTest.cpp"
