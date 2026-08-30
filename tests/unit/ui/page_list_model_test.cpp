#include "ui/widget/PageListModel.h"

#include <QtTest>

using manga::ui::widget::PageListEntry;
using manga::ui::widget::PageListModel;
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

class PageListModelTest : public QObject {
    Q_OBJECT
private slots:
    void setPagesPopulatesRows() {
        PageListModel model;
        QVector<PageListEntry> pages{makeEntry(0), makeEntry(1), makeEntry(2)};
        model.setPages(pages);
        QCOMPARE(model.rowCount(), 3);
        const QModelIndex index = model.index(1);
        QCOMPARE(index.data(PageListModel::OrderIndexRole).toInt(), 1);
        QCOMPARE(index.data(PageListModel::DisplayNameRole).toString(), QStringLiteral("page_1001.png"));
        QCOMPARE(index.data(PageListModel::ThumbnailStatusRole).toInt(), static_cast<int>(ThumbnailStatus::NotLoaded));
        QCOMPARE(index.data(Qt::AccessibleTextRole).toString(), QStringLiteral("第 2 页，page_1001.png"));
    }

    void identicalSetPagesSkipsReset() {
        PageListModel model;
        const QVector<PageListEntry> pages{makeEntry(0), makeEntry(1)};
        model.setPages(pages);
        QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
        model.setPages(pages);
        QCOMPARE(spy.size(), 0);
    }

    void thumbnailStatusTransitionsByPageId() {
        PageListModel model;
        const auto entry = makeEntry(0);
        model.setPages({entry});
        model.setThumbnailStatus(entry.id, ThumbnailStatus::Loading);
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Loading));
        model.setThumbnailStatus(entry.id, ThumbnailStatus::Failed, QStringLiteral("无法解码"));
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailStatusRole).toInt(),
                 static_cast<int>(ThumbnailStatus::Failed));
        QCOMPARE(model.index(0).data(PageListModel::ThumbnailErrorRole).toString(), QStringLiteral("无法解码"));
        model.setThumbnailStatus(manga::domain::PageId::generate(), ThumbnailStatus::Ready); // 未知 ID：忽略
        QCOMPARE(model.rowCount(), 1);
    }

    void modelHoldsNoPixmaps() {
        // 结构性断言：Model 的 Role 集合里没有图像 Role（像素只进 QCache）。
        PageListModel model;
        model.setPages({makeEntry(0)});
        QVERIFY(!model.index(0).data(Qt::DecorationRole).isValid());
    }
};

QTEST_MAIN(PageListModelTest)
#include "page_list_model_test.moc"
