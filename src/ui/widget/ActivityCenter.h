#pragma once

#include "application/operation/OperationId.h"
#include "application/operation/OperationSnapshot.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QVector>
#include <QWidget>

#include <set>

class QVBoxLayout;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace manga::ui::widget {

class ActivityCenter : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    explicit ActivityCenter(QWidget* parent = nullptr);
    ~ActivityCenter() override = default;

    void setOperations(const QVector<application::OperationSnapshot>& operations);
    void dismissFailedOperation(const application::OperationId& id);
    void clearAllDismissed();
    void updateParentGeometry(const QSize& parentSize);

    [[nodiscard]] int visibleItemCount() const noexcept {
        return m_visibleItemCount;
    }

Q_SIGNALS:
    void cancelRequested(const application::OperationId& id);
    void operationActivated(const application::OperationId& id,
                            const domain::ProjectId& projectId,
                            const domain::PageId& pageId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void onThemeUpdated() override;

private:
    void rebuildItems(const QVector<application::OperationSnapshot>& operations);
    void startShowAnimation();
    void startHideAnimation();
    void updateTargetPosition(const QSize& parentSize);

    QVBoxLayout* m_layout{nullptr};
    QGraphicsOpacityEffect* m_opacityEffect{nullptr};
    QPropertyAnimation* m_opacityAnim{nullptr};
    QTimer* m_hideTimer{nullptr};

    std::set<application::OperationId> m_dismissedFailedIds;
    int m_visibleItemCount{0};
    int m_targetX{0};
    int m_targetY{0};
    bool m_isHiding{false};
};

} // namespace manga::ui::widget
