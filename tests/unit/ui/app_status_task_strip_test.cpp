#include "application/operation/OperationSnapshot.h"
#include "ui/model/OperationListModel.h"
#include "ui/widget/AppStatusTaskStrip.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga::application;
using namespace manga::domain;
using namespace manga::ui::widget;

class AppStatusTaskStripTest : public QObject {
    Q_OBJECT

private:
    OperationSnapshot createSampleSnapshot(
        const QString& target, OperationStage stage, OperationState state, qint64 completed = 0, qint64 total = 100) {
        OperationSnapshot s;
        s.id = OperationId::generate();
        s.descriptor.projectId = ProjectId::generate();
        s.descriptor.pageId = PageId::generate();
        s.descriptor.stage = stage;
        s.descriptor.displayTarget = target;
        s.descriptor.cancellable = true;
        s.state = state;
        s.completed = completed;
        s.total = total;
        s.statusText = QStringLiteral("Sample status");
        return s;
    }

private Q_SLOTS:
    void initialLayoutAndMetrics() {
        AppStatusTaskStrip strip;
        QCOMPARE(strip.sizeHint().height(), 32);
        QCOMPARE(strip.documentStatus().status, DocumentSaveStatus::NoDocument);
        QCOMPARE(strip.environmentStatus().condition, EnvironmentCondition::Normal);
        QVERIFY(!strip.isFlyoutOpen());
    }

    void documentStatusChanges() {
        AppStatusTaskStrip strip;

        DocumentStatusSnapshot dirtyStatus;
        dirtyStatus.status = DocumentSaveStatus::Dirty;
        dirtyStatus.message = QStringLiteral("有未保存更改");
        strip.setDocumentStatus(dirtyStatus);
        QCOMPARE(strip.documentStatus().status, DocumentSaveStatus::Dirty);

        DocumentStatusSnapshot savingStatus;
        savingStatus.status = DocumentSaveStatus::Saving;
        savingStatus.message = QStringLiteral("正在保存");
        strip.setDocumentStatus(savingStatus);
        QCOMPARE(strip.documentStatus().status, DocumentSaveStatus::Saving);

        DocumentStatusSnapshot failedStatus;
        failedStatus.status = DocumentSaveStatus::SaveFailed;
        failedStatus.message = QStringLiteral("保存失败");
        failedStatus.failureDetail = QStringLiteral("Disk full");
        strip.setDocumentStatus(failedStatus);
        QCOMPARE(strip.documentStatus().status, DocumentSaveStatus::SaveFailed);

        DocumentStatusSnapshot savedStatus;
        savedStatus.status = DocumentSaveStatus::Saved;
        savedStatus.message = QStringLiteral("所有更改已保存");
        strip.setDocumentStatus(savedStatus);
        QCOMPARE(strip.documentStatus().status, DocumentSaveStatus::Saved);
    }

    void priorityOrderingUnderConstrainedSpace() {
        AppStatusTaskStrip strip;
        strip.resize(1200, 32);

        DocumentStatusSnapshot failedStatus;
        failedStatus.status = DocumentSaveStatus::SaveFailed;
        failedStatus.message = QStringLiteral("保存失败");
        strip.setDocumentStatus(failedStatus);

        auto op1 =
            createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running, 50, 100);
        strip.setOperations({op1});

        EnvironmentStatusSnapshot envStatus;
        envStatus.condition = EnvironmentCondition::Offline;
        envStatus.statusText = QStringLiteral("离线");
        strip.setEnvironmentStatus(envStatus);

        // At width < 720, SaveFailed has highest priority over running task and offline env
        strip.resize(600, 32);
        QCOMPARE(strip.highestPrioritySection(), AppStatusTaskStrip::Section::Document);

        // When Document is Saved (priority 4) and there is a Running task (priority 2)
        DocumentStatusSnapshot savedStatus;
        savedStatus.status = DocumentSaveStatus::Saved;
        savedStatus.message = QStringLiteral("所有更改已保存");
        strip.setDocumentStatus(savedStatus);
        QCOMPARE(strip.highestPrioritySection(), AppStatusTaskStrip::Section::ActiveTask);

        // When no tasks, but Offline (priority 3) vs Saved (priority 4)
        strip.setOperations({});
        QCOMPARE(strip.highestPrioritySection(), AppStatusTaskStrip::Section::Environment);
    }

    void activeTaskSingleAndMultipleSummary() {
        AppStatusTaskStrip strip;
        auto op1 = createSampleSnapshot(
            QStringLiteral("第 3 页"), OperationStage::Translate, OperationState::Running, 72, 100);
        strip.setOperations({op1});

        QString singleSummary = strip.activeTaskSummaryText();
        QVERIFY(singleSummary.contains(QStringLiteral("第 3 页")));
        QVERIFY(singleSummary.contains(QStringLiteral("72%")) || singleSummary.contains(QStringLiteral("翻译")));

        auto op2 = createSampleSnapshot(QStringLiteral("第 4 页"), OperationStage::Ocr, OperationState::Queued, 0, 100);
        auto op3 =
            createSampleSnapshot(QStringLiteral("第 5 页"), OperationStage::Clear, OperationState::Queued, 0, 100);
        strip.setOperations({op1, op2, op3});

        QString multiSummary = strip.activeTaskSummaryText();
        QVERIFY(multiSummary.contains(QStringLiteral("另有 2 个任务")));
    }

    void completionSummaryTransitions() {
        AppStatusTaskStrip strip;
        auto op1 =
            createSampleSnapshot(QStringLiteral("第 1 页"), OperationStage::Ocr, OperationState::Running, 50, 100);
        strip.setOperations({op1});

        // Complete the operation
        op1.state = OperationState::Completed;
        op1.completed = 100;
        strip.setOperations({op1});

        // Immediately after completion, completion summary is active
        QVERIFY(strip.hasCompletionSummary());

        // Wait for timer to settle completion summary (set test-friendly delay or trigger timeout)
        strip.settleCompletionSummaryForTest();
        QVERIFY(!strip.hasCompletionSummary());
    }

    void userInteractionsAndSignals() {
        AppStatusTaskStrip strip;
        const auto op =
            createSampleSnapshot(QStringLiteral("第 2 页"), OperationStage::Ocr, OperationState::Running, 50, 100);
        strip.setOperations({op});

        QSignalSpy activateSpy(&strip, &AppStatusTaskStrip::operationActivated);
        QSignalSpy cancelSpy(&strip, &AppStatusTaskStrip::cancelRequested);
        QSignalSpy saveDetailSpy(&strip, &AppStatusTaskStrip::saveFailureDetailsRequested);

        strip.triggerActiveTaskClickForTest();
        QCOMPARE(activateSpy.count(), 1);
        const auto actArgs = activateSpy.takeFirst();
        QCOMPARE(actArgs.at(0).value<OperationId>(), op.id);
        QCOMPARE(actArgs.at(1).value<ProjectId>(), op.descriptor.projectId);
        QCOMPARE(actArgs.at(2).value<PageId>(), op.descriptor.pageId);

        strip.triggerCancelTaskForTest(op.id);
        QCOMPARE(cancelSpy.count(), 1);
        QCOMPARE(cancelSpy.takeFirst().at(0).value<OperationId>(), op.id);

        DocumentStatusSnapshot fail;
        fail.status = DocumentSaveStatus::SaveFailed;
        strip.setDocumentStatus(fail);
        strip.triggerDocumentStatusClickForTest();
        QCOMPARE(saveDetailSpy.count(), 1);
    }

    void flyoutLifecycleAndLeakCheck() {
        AppStatusTaskStrip strip;
        strip.show();
        QCoreApplication::processEvents();

        for (int i = 0; i < 50; ++i) {
            strip.openTaskFlyout();
            QVERIFY(strip.isFlyoutOpen());
            strip.closeTaskFlyout();
            QVERIFY(!strip.isFlyoutOpen());
        }
    }

    void narrowScreenHotspotUpdatesWithoutResize() {
        AppStatusTaskStrip strip;
        strip.resize(600, 32);
        strip.show();
        QCoreApplication::processEvents();

        QSignalSpy activateSpy(&strip, &AppStatusTaskStrip::operationActivated);
        QSignalSpy saveDetailSpy(&strip, &AppStatusTaskStrip::saveFailureDetailsRequested);

        // 1. Set SaveFailed (Document priority)
        DocumentStatusSnapshot failStatus;
        failStatus.status = DocumentSaveStatus::SaveFailed;
        failStatus.message = QStringLiteral("保存失败");
        strip.setDocumentStatus(failStatus);

        QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(50, 16));
        QCOMPARE(saveDetailSpy.count(), 1);
        QCOMPARE(activateSpy.count(), 0);

        // 2. Change Document to Saved and add Running task (without resizing!)
        DocumentStatusSnapshot savedStatus;
        savedStatus.status = DocumentSaveStatus::Saved;
        strip.setDocumentStatus(savedStatus);

        const auto op =
            createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running, 50, 100);
        strip.setOperations({op});

        // Click at same position should now trigger operationActivated because hotspot was updated
        QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(50, 16));
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).value<OperationId>(), op.id);
    }
};

QTEST_MAIN(AppStatusTaskStripTest)
#include "app_status_task_strip_test.moc"
