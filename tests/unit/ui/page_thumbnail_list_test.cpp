#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <QtTest>

using manga::ui::widget::PageListEntry;
using manga::ui::widget::PageThumbnailList;
using manga::ui::widget::ThumbnailStatus;

namespace {
PageListEntry makeEntry(int order) {
    PageListEntry entry;
    entry.id = manga::domain::PageId::generate();
    entry.orderIndex = order;
    entry.displayName = QStringLiteral("page_%1.png").arg(1000 + order);
    entry.aspectRatio = 2.0 / 3.0;
    return entry;
}
} // namespace

class PageThumbnailListTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<manga::domain::PageId>("manga::domain::PageId");
        qRegisterMetaType<manga::domain::PageId>("::manga::domain::PageId");
    }

    void visibleRowsRequestThumbnailsOnlyOnce() {
        PageThumbnailList list;
        QSignalSpy spy(&list, &PageThumbnailList::thumbnailRequested);
        QVector<PageListEntry> pages;
        for (int i = 0; i < 10; ++i)
            pages.append(makeEntry(i));
        list.model()->setPages(pages);
        list.resize(220, 400);
        list.show();
        QCoreApplication::processEvents();
        // The first requestVisibleThumbnails should have happened during show/paint
        list.requestVisibleThumbnails(); // Explicit call to ensure it covers if not painted
        const int firstCount = spy.size();
        QVERIFY(firstCount > 0);
        QVERIFY(firstCount <= 10);
        list.requestVisibleThumbnails(); // 已置 Loading 的行不重复请求
        QCOMPARE(spy.size(), firstCount);
    }

    void thumbnailCacheHonorsSixtyFourMiBBudget() {
        PageThumbnailList list;
        // 每张 1024x1024 RGBA = 4 MiB = 4096 KiB cost；64 MiB 预算最多容纳 16 张。
        const QImage image(1024, 1024, QImage::Format_RGBA8888);
        for (int i = 0; i < 40; ++i) {
            list.insertThumbnail(manga::domain::PageId::generate(), image);
        }
        QVERIFY(list.thumbnailCacheTotalCostForTest() <= 64 * 1024);
    }

    void failedThumbnailCanBeRetriedWithForce() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry});
        list.markThumbnailFailed(entry.id, QStringLiteral("无法解码"));
        QCOMPARE(list.model()->index(0).data(manga::ui::widget::PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Failed));
        // force 重试由 delegate 热点触发；这里直接验证信号路径存在：
        QSignalSpy spy(&list, &PageThumbnailList::thumbnailRequested);
        list.requestThumbnailForTest(entry.id, true);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(1).toBool(), true);
    }

    void resetLoadingThumbnailsRetriggersVisibleRows() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry});
        list.model()->setThumbnailStatus(entry.id, ThumbnailStatus::Loading);
        QSignalSpy spy(&list, &PageThumbnailList::thumbnailRequested);
        list.resetLoadingThumbnails();
        QCOMPARE(list.model()->index(0).data(manga::ui::widget::PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Loading));
        QCOMPARE(spy.size(), 1);
    }

    void currentPageSelectionDoesNotReemitActivation() {
        PageThumbnailList list;
        const auto entry = makeEntry(0);
        list.model()->setPages({entry, makeEntry(1)});
        list.show();
        QSignalSpy spy(&list, &PageThumbnailList::pageActivated);
        list.setCurrentPageId(entry.id);
        QCOMPARE(spy.size(), 0);
    }
};

QTEST_MAIN(PageThumbnailListTest)
#include "page_thumbnail_list_test.moc"
