#include "ui/widget/PageThumbnailList.h"

#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailDelegate.h"

#include <FluentQt/Collections.h>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace manga::ui::widget {

PageThumbnailList::PageThumbnailList(QWidget* parent)
    : QWidget(parent), m_model(new PageListModel(this)), m_view(new fluent::collections::ListView(this)),
      m_shimmerTimer(new QTimer(this)) {
    m_thumbnailCache.setMaxCost(64 * 1024);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_view->setModel(m_model);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);

    m_delegate = new PageThumbnailDelegate(this, m_view);
    m_view->setItemDelegate(m_delegate);

    connect(m_view, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (auto pageId = m_model->pageIdAt(index.row())) {
            emit pageActivated(*pageId);
        }
    });

    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { requestVisibleThumbnails(); });

    m_shimmerTimer->setInterval(50);
    connect(m_shimmerTimer, &QTimer::timeout, this, [this]() {
        m_shimmerProgress += 50.0 / 1600.0;
        if (m_shimmerProgress >= 1.0)
            m_shimmerProgress -= 1.0;
        m_delegate->setShimmerProgress(m_shimmerProgress);
        m_view->viewport()->update();
    });

    m_view->viewport()->installEventFilter(this);

    onThemeUpdated();
}

bool PageThumbnailList::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_view->viewport() && event->type() == QEvent::Paint) {
        requestVisibleThumbnails();
    }
    return QWidget::eventFilter(obj, event);
}

void PageThumbnailList::onThemeUpdated() {
    m_shimmerPalette = fluent::status_info::ShimmerPainter::paletteFromTheme(themeColorsRef(), false);
    m_delegate->setShimmerPalette(m_shimmerPalette);
    m_view->viewport()->update();
}

void PageThumbnailList::setCurrentPageId(const domain::PageId& pageId) {
    int row = m_model->rowForPageId(pageId);
    if (row >= 0) {
        QSignalBlocker blocker(m_view->selectionModel());
        m_view->setCurrentIndex(m_model->index(row));
    }
}

void PageThumbnailList::requestVisibleThumbnails() {
    QRect viewportRect = m_view->viewport()->rect();
    int firstRow = m_view->indexAt(viewportRect.topLeft()).row();
    int lastRow = m_view->indexAt(viewportRect.bottomLeft()).row();

    if (firstRow < 0) {
        firstRow = 0;
        lastRow = m_model->rowCount() - 1; // if layout not done, maybe ask all? or just return.
    }

    if (lastRow < 0)
        lastRow = m_model->rowCount() - 1;

    firstRow = std::max(0, firstRow - 2);
    lastRow = std::min(m_model->rowCount() - 1, lastRow + 2);

    qDebug() << "requestVisibleThumbnails: firstRow=" << firstRow << " lastRow=" << lastRow
             << " rowCount=" << m_model->rowCount();

    bool hasLoading = false;
    for (int i = firstRow; i <= lastRow; ++i) {
        auto status = static_cast<ThumbnailStatus>(m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt());
        if (status == ThumbnailStatus::NotLoaded) {
            if (auto pageId = m_model->pageIdAt(i)) {
                qDebug() << "emitting requested for row" << i;
                m_model->setThumbnailStatus(*pageId, ThumbnailStatus::Loading);
                emit thumbnailRequested(*pageId, false);
                hasLoading = true;
            }
        } else if (status == ThumbnailStatus::Loading) {
            hasLoading = true;
        }
    }

    if (hasLoading && !m_shimmerTimer->isActive()) {
        m_shimmerTimer->start();
    }

    updateShimmerTimer();
}

void PageThumbnailList::updateShimmerTimer() {
    bool hasLoading = false;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt() ==
            static_cast<int>(ThumbnailStatus::Loading)) {
            hasLoading = true;
            break;
        }
    }
    if (hasLoading && !m_shimmerTimer->isActive()) {
        m_shimmerTimer->start();
    } else if (!hasLoading && m_shimmerTimer->isActive()) {
        m_shimmerTimer->stop();
    }
}

void PageThumbnailList::insertThumbnail(const domain::PageId& pageId, const QImage& image) {
    int cost = (image.sizeInBytes() + 1023) / 1024;
    cost = std::max(1, cost);
    m_thumbnailCache.insert(QString::fromStdString(pageId.toString()), new QPixmap(QPixmap::fromImage(image)), cost);
    m_model->setThumbnailStatus(pageId, ThumbnailStatus::Ready);
    updateShimmerTimer();
}

void PageThumbnailList::markThumbnailFailed(const domain::PageId& pageId, const QString& safeMessage) {
    m_model->setThumbnailStatus(pageId, ThumbnailStatus::Failed, safeMessage);
    updateShimmerTimer();
}

void PageThumbnailList::resetLoadingThumbnails() {
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto status = static_cast<ThumbnailStatus>(m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt());
        if (status == ThumbnailStatus::Loading) {
            if (auto pageId = m_model->pageIdAt(i)) {
                m_model->setThumbnailStatus(*pageId, ThumbnailStatus::NotLoaded);
            }
        }
    }
    updateShimmerTimer();
}

const QPixmap* PageThumbnailList::thumbnail(const domain::PageId& pageId) const {
    return m_thumbnailCache.object(QString::fromStdString(pageId.toString()));
}

int PageThumbnailList::thumbnailCacheTotalCostForTest() const {
    return m_thumbnailCache.totalCost();
}

void PageThumbnailList::requestThumbnailForTest(const domain::PageId& pageId, bool force) {
    emit thumbnailRequested(pageId, force);
}

} // namespace manga::ui::widget
