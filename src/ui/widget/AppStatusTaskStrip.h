#pragma once

#include "application/operation/OperationSnapshot.h"
#include "domain/model/Uuid.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QPointer>
#include <QWidget>

#include <optional>

namespace fluent::dialogs_flyouts {
class Flyout;
}

namespace manga::ui::model {
class OperationListModel;
}

namespace manga::ui::delegate {
class OperationRowDelegate;
}

namespace manga::ui::widget {

enum class DocumentSaveStatus {
    NoDocument,
    Saved,
    Dirty,
    Saving,
    SaveFailed,
    ReadOnly
};

struct DocumentStatusSnapshot {
    DocumentSaveStatus status{DocumentSaveStatus::NoDocument};
    QString message;
    QString failureDetail;
    bool operator==(const DocumentStatusSnapshot&) const = default;
};

enum class EnvironmentCondition {
    Normal,
    GpuAvailable,
    CpuMode,
    Offline,
    ModelMissing,
    MemoryPressure
};

struct EnvironmentStatusSnapshot {
    EnvironmentCondition condition{EnvironmentCondition::Normal};
    QString statusText;
    bool operator==(const EnvironmentStatusSnapshot&) const = default;
};

class AppStatusTaskStrip : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    enum class Section {
        Document,
        ActiveTask,
        Environment
    };

    explicit AppStatusTaskStrip(QWidget* parent = nullptr);
    ~AppStatusTaskStrip() override;

    void setOperations(const QVector<application::OperationSnapshot>& operations);
    void setDocumentStatus(const DocumentStatusSnapshot& docStatus);
    void setEnvironmentStatus(const EnvironmentStatusSnapshot& envStatus);

    [[nodiscard]] const DocumentStatusSnapshot& documentStatus() const noexcept {
        return m_docStatus;
    }
    [[nodiscard]] const EnvironmentStatusSnapshot& environmentStatus() const noexcept {
        return m_envStatus;
    }
    [[nodiscard]] model::OperationListModel* operationModel() const noexcept {
        return m_operationModel;
    }

    [[nodiscard]] Section highestPrioritySection() const;
    [[nodiscard]] QString activeTaskSummaryText() const;
    [[nodiscard]] bool hasCompletionSummary() const noexcept {
        return m_hasCompletionSummary;
    }
    [[nodiscard]] bool isFlyoutOpen() const noexcept {
        return !m_taskFlyout.isNull();
    }

    void openTaskFlyout();
    void closeTaskFlyout();

    // Test hooks
    void settleCompletionSummaryForTest();
    void triggerActiveTaskClickForTest();
    void triggerCancelTaskForTest(const application::OperationId& id);
    void triggerDocumentStatusClickForTest();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    void onThemeUpdated() override;

Q_SIGNALS:
    void operationActivated(const application::OperationId& id,
                            const domain::ProjectId& projectId,
                            const domain::PageId& pageId);
    void cancelRequested(const application::OperationId& id);
    void saveFailureDetailsRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool hasVisibleStatusContent() const;
    void updateResponsiveLayout();
    void checkTaskCompletionTransitions(const QVector<application::OperationSnapshot>& newOperations);
    void updateActiveTaskProjection();

    DocumentStatusSnapshot m_docStatus;
    EnvironmentStatusSnapshot m_envStatus;
    model::OperationListModel* m_operationModel{nullptr};

    std::optional<application::OperationSnapshot> m_primaryActiveTask;
    int m_activeTaskCount{0};
    bool m_hasCompletionSummary{false};
    QString m_completionText;
    QTimer* m_completionTimer{nullptr};

    QPointer<fluent::dialogs_flyouts::Flyout> m_taskFlyout;

    // Hotspot regions for click handling
    QRect m_docHotspot;
    QRect m_taskHotspot;
    QRect m_envHotspot;
    QRect m_menuBtnHotspot;
};

} // namespace manga::ui::widget
