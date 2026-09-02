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
namespace {
// Fallback row height used to size the visible range before the view has laid out any row.
constexpr int kThumbnailRowHeightHint = 88;
} // namespace

PageThumbnailList::PageThumbnailList(QWidget* parent)
    : QWidget(parent), m_model(new PageListModel(this)), m_view(new QListView(this)), m_shimmerTimer(new QTimer(this)) {
    m_thumbnailCache.setMaxCost(64 * 1024); // Costs are in KiB (insertThumbnail), so 64 MiB of pixmaps.
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
    // The view marks the row under the pointer with State_MouseOver only while the viewport
    // receives hover events, so the delegate can paint the Fluent hover fill.
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->viewport()->setMouseTracking(true);

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

    connect(
        m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { scheduleVisibleThumbnailRequest(); });

    connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
        // A reset clears the view index and scroll offset, so the tracked page is stale until the
        // next setCurrentPageId re-arms it.
        m_trackedPageId = domain::PageId();
        updateCountLabel();
        scheduleVisibleThumbnailRequest();
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this]() {
        updateCountLabel();
        scheduleVisibleThumbnailRequest();
    });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this]() {
        updateCountLabel();
        scheduleVisibleThumbnailRequest();
    });
    updateCountLabel();

    m_shimmerTimer->setInterval(50);
    connect(m_shimmerTimer, &QTimer::timeout, this, [this]() {
        m_shimmerProgress += 50.0 / 1600.0;
        if (m_shimmerProgress >= 1.0)
            m_shimmerProgress -= 1.0;
        m_delegate->setShimmerProgress(m_shimmerProgress);
        m_view->viewport()->update();
    });

    onThemeUpdated();
}

void PageThumbnailList::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    scheduleVisibleThumbnailRequest();
}

void PageThumbnailList::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    scheduleVisibleThumbnailRequest();
}

void PageThumbnailList::scheduleVisibleThumbnailRequest() {
    if (m_pendingThumbnailRequest) {
        return;
    }
    m_pendingThumbnailRequest = true;
    QTimer::singleShot(0, this, [this]() {
        m_pendingThumbnailRequest = false;
        requestVisibleThumbnails();
    });
}

void PageThumbnailList::onThemeUpdated() {
    m_shimmerPalette = fluent::status_info::ShimmerPainter::paletteFromTheme(themeColorsRef(), false);
    m_delegate->setShimmerPalette(m_shimmerPalette);
    m_view->viewport()->update();
}

void PageThumbnailList::setCurrentPageId(const domain::PageId& pageId) {
    int row = m_model->rowForPageId(pageId);
    if (row < 0) {
        return;
    }

    const QModelIndex index = m_model->index(row);
    {
        QSignalBlocker blocker(m_view->selectionModel());
        m_view->setCurrentIndex(index);
        m_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    // Only scroll to the newly active page when the page actually changed and the
    // user is not manually scrolling. Calling scrollTo on every re-render jumps the
    // list back to the current page whenever the user has scrolled away to browse.
    if (pageId != m_trackedPageId) {
        m_trackedPageId = pageId;
        if (!m_view->verticalScrollBar()->isSliderDown()) {
            m_view->scrollTo(index);
        }
        // The target page may already be inside the viewport so no scroll signal fires;
        // trigger a thumbnail scan once to cover that case.
        scheduleVisibleThumbnailRequest();
    }
}

bool PageThumbnailList::isCurrentRow(int row) const {
    return m_view != nullptr && m_view->currentIndex().row() == row;
}

void PageThumbnailList::updateCountLabel() {
    m_headerCount->setText(tr("%1 页").arg(m_model->rowCount()));
}

void PageThumbnailList::requestVisibleThumbnails() {
    const QRect viewportRect = m_view->viewport()->rect();
    const int centerX = viewportRect.center().x();
    int firstRow = m_view->indexAt(QPoint(centerX, viewportRect.top())).row();
    int lastRow = m_view->indexAt(QPoint(centerX, viewportRect.bottom() - 1)).row();

    if (firstRow < 0) {
        firstRow = 0;
    }

    if (lastRow < 0) {
        const int visibleCapacity = viewportRect.height() / kThumbnailRowHeightHint + 1;
        lastRow = firstRow + visibleCapacity;
    }

    firstRow = std::max(0, firstRow - 2);
    lastRow = std::min(m_model->rowCount() - 1, lastRow + 2);

    bool hasLoading = false;
    QVector<domain::PageId> visible;
    for (int i = firstRow; i <= lastRow; ++i) {
        auto status = static_cast<ThumbnailStatus>(m_model->index(i).data(PageListModel::ThumbnailStatusRole).toInt());
        auto pageIdOpt = m_model->pageIdAt(i);

        if (!pageIdOpt)
            continue;

        const auto& pageId = *pageIdOpt;
        visible.append(pageId);
        bool evicted = (status == ThumbnailStatus::Ready && !thumbnail(pageId));

        if (status == ThumbnailStatus::NotLoaded || evicted) {
            m_model->setThumbnailStatus(pageId, ThumbnailStatus::Loading);
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

    // Emit visiblePagesChanged only when the visible set actually changes. Using a
    // QSet comparison avoids false positives caused by reordering within the viewport.
    QSet<QString> visibleSet;
    visibleSet.reserve(visible.size());
    for (const auto& id : visible) {
        visibleSet.insert(QString::fromStdString(id.toString()));
    }
    if (visibleSet != m_lastVisiblePageIdSet) {
        m_lastVisiblePageIdSet = std::move(visibleSet);
        m_lastVisiblePageIds = visible;
        Q_EMIT visiblePagesChanged(m_lastVisiblePageIds);
    }
}

void PageThumbnailList::insertThumbnail(const domain::PageId& pageId, const QImage& image) {
    int cost = (image.sizeInBytes() + 1023) / 1024;
    cost = std::max(1, cost);
    m_thumbnailCache.insert(QString::fromStdString(pageId.toString()), new QPixmap(QPixmap::fromImage(image)), cost);
    m_model->setThumbnailStatus(pageId, ThumbnailStatus::Ready);
    // No scheduleVisibleThumbnailRequest: the model dataChanged signal will cause a repaint,
    // and the status is now Ready so the next scan will not re-request this row.
    // A re-check is only needed when cache eviction drops a previously Ready row, which is
    // detected during the next scroll- or resize-driven scan.
}

void PageThumbnailList::markThumbnailFailed(const domain::PageId& pageId, const QString& safeMessage) {
    m_model->setThumbnailStatus(pageId, ThumbnailStatus::Failed, safeMessage);
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
