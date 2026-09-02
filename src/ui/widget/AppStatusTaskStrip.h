#pragma once

#include "application/operation/OperationSnapshot.h"
#include "domain/model/Uuid.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Windowing.h>
#include <QEvent>
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

    [[nodiscard]] Section highestPrioritySection() const {
        return Section::Document;
    }
    [[nodiscard]] QString activeTaskSummaryText() const {
        return m_message;
    }
    [[nodiscard]] bool hasCompletionSummary() const noexcept {
        return false;
    }
    [[nodiscard]] bool isFlyoutOpen() const noexcept {
        return false;
    }

    void openTaskFlyout() {}
    void closeTaskFlyout() {}

    /// Displays a transient informational message in the status strip that automatically disappears after timeoutMs
    /// (default 3 seconds).
    void showMessage(const QString& message, int timeoutMs = 3000);
    void clearMessage();
    [[nodiscard]] QString currentMessage() const {
        return m_message;
    }
    [[nodiscard]] bool hasMessage() const {
        return !m_message.isEmpty();
    }

    // Test hooks
    void settleCompletionSummaryForTest() {}
    void triggerActiveTaskClickForTest() {}
    void triggerCancelTaskForTest(const application::OperationId&) {}
    void triggerDocumentStatusClickForTest() {}

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
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    DocumentStatusSnapshot m_docStatus;
    EnvironmentStatusSnapshot m_envStatus;
    model::OperationListModel* m_operationModel{nullptr};

    QString m_message;
    QTimer* m_messageTimer{nullptr};
};

} // namespace manga::ui::widget
