#include "ui/widget/PageThumbnailList.h"

#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailDelegate.h"

#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QListView>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace manga::ui::widget {

PageThumbnailList::PageThumbnailList(QWidget* parent)
    : QWidget(parent), m_model(new PageListModel(this)), m_view(new QListView(this)), m_shimmerTimer(new QTimer(this)) {
    m_thumbnailCache.setMaxCost(64 * 1024);
    setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(m_view);
    m_view->setFocusPolicy(Qt::StrongFocus);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(16, 12, 16, 8);
    m_headerTitle = new fluent::textfields::Label(tr("页面"), this);
    m_headerTitle->setFluentTypography(Typography::FontRole::BodyStrong);
    m_headerCount = new fluent::textfields::Label(this);
    m_headerCount->setFluentTypography(Typography::FontRole::Caption);
    m_headerCount->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    headerLayout->addWidget(m_headerTitle);
    headerLayout->addWidget(m_headerCount, 1, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(headerLayout);

    layout->addWidget(m_view, 1);

    // FluentQt ListView swallows mouse press/release on valid rows, which breaks
    // QAbstractItemView::clicked and delegate editorEvent hotspots; a raw QListView
    // with Fluent token painting in the delegate is the documented porting gap.
    m_view->setModel(m_model);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setAutoFillBackground(false);
    m_view->viewport()->setAutoFillBackground(false);

    m_delegate = new PageThumbnailDelegate(this, m_view);
    m_view->setItemDelegate(m_delegate);

    connect(m_view, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) ||
            QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
            return;
        }
        if (auto pageId = m_model->pageIdAt(index.row())) {
            Q_EMIT pageActivated(*pageId);
        }
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        QVector<domain::PageId> selected;
        for (const auto& index : m_view->selectionModel()->selectedIndexes()) {
            if (const auto pageId = m_model->pageIdAt(index.row())) {
                selected.append(*pageId);
            }
        }
        Q_EMIT pagesSelected(selected);
    });

    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { requestVisibleThumbnails(); });

    connect(m_model, &QAbstractItemModel::modelReset, this, [this]() { updateCountLabel(); });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this]() { updateCountLabel(); });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this]() { updateCountLabel(); });
    updateCountLabel();

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
        const QModelIndex index = m_model->index(row);
        m_view->setCurrentIndex(index);
        m_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        if (!m_view->verticalScrollBar()->isSliderDown()) {
            m_view->scrollTo(index);
        }
    }
}

void PageThumbnailList::updateCountLabel() {
    m_headerCount->setText(tr("%1 页").arg(m_model->rowCount()));
}

void PageThumbnailList::requestVisibleThumbnails() {
    QRect viewportRect = m_view->viewport()->rect();
    int firstRow = m_view->indexAt(viewportRect.topLeft()).row();
    int lastRow = m_view->indexAt(viewportRect.bottomLeft()).row();

    if (firstRow < 0) {
        firstRow = 0;
    }

    if (lastRow < 0) {
        int visibleCapacity = viewportRect.height() / 88 + 1;
        lastRow = firstRow + visibleCapacity;
    }

    firstRow = std::max(0, firstRow - 2);
    lastRow = std::min(m_model->rowCount() - 1, lastRow + 2);

    bool hasLoading = false;
    for (int i = firstRow; i <= lastRow; ++i) {
        auto status = static_cast<ThumbnailStatus>(m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt());
        auto pageIdOpt = m_model->pageIdAt(i);

        if (!pageIdOpt)
            continue;

        const auto& pageId = *pageIdOpt;
        bool evicted = (status == ThumbnailStatus::Ready && !thumbnail(pageId));

        if (status == ThumbnailStatus::NotLoaded || evicted) {
            m_model->setThumbnailStatus(pageId, ThumbnailStatus::Loading);
            qInfo().noquote() << "[ThumbnailUI] request page=" << QString::fromStdString(pageId.toString())
                              << "reason=" << (evicted ? "cache-evicted" : "not-loaded");
            Q_EMIT thumbnailRequested(pageId, false);
            hasLoading = true;
        } else if (status == ThumbnailStatus::Loading) {
            hasLoading = true;
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
    qInfo().noquote() << "[ThumbnailUI] ready page=" << QString::fromStdString(pageId.toString())
                      << "size=" << image.size() << "cacheCostKiB=" << m_thumbnailCache.totalCost();
    // requestVisibleThumbnails() will be called on paintEvent triggered by dataChanged
}

void PageThumbnailList::markThumbnailFailed(const domain::PageId& pageId, const QString& safeMessage) {
    m_model->setThumbnailStatus(pageId, ThumbnailStatus::Failed, safeMessage);
    qWarning().noquote() << "[ThumbnailUI] failed page=" << QString::fromStdString(pageId.toString())
                         << "message=" << safeMessage;
}

void PageThumbnailList::resetLoadingThumbnails() {
    int resetCount = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto status = static_cast<ThumbnailStatus>(m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt());
        if (status == ThumbnailStatus::Loading) {
            if (auto pageId = m_model->pageIdAt(i)) {
                m_model->setThumbnailStatus(*pageId, ThumbnailStatus::NotLoaded);
                ++resetCount;
            }
        }
    }
    if (resetCount > 0) {
        qInfo() << "[ThumbnailUI] reset loading rows=" << resetCount;
    }
    requestVisibleThumbnails();
}

const QPixmap* PageThumbnailList::thumbnail(const domain::PageId& pageId) const {
    return m_thumbnailCache.object(QString::fromStdString(pageId.toString()));
}

int PageThumbnailList::thumbnailCacheTotalCostForTest() const {
    return m_thumbnailCache.totalCost();
}

void PageThumbnailList::requestThumbnailForTest(const domain::PageId& pageId, bool force) {
    Q_EMIT thumbnailRequested(pageId, force);
}

void PageThumbnailList::setPageProcessingStatus(const domain::PageId& pageId, int status) {
    if (m_model != nullptr) {
        m_model->setProcessingStatus(pageId, status);
    }
}

} // namespace manga::ui::widget
