#include "application/operation/OperationSnapshot.h"
#include "ui/model/OperationListModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga::application;
using namespace manga::domain;
using namespace manga::ui::model;

class OperationListModelTest : public QObject {
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
    void emptyModel() {
        OperationListModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.data(model.index(0, 0), Qt::DisplayRole).isValid());
        QVERIFY(model.operations().isEmpty());
    }

    void setOperationsPopulatesModel() {
        OperationListModel model;
        const auto op1 =
            createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running, 40, 100);
        const auto op2 =
            createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Queued, 0, 100);

        model.setOperations({op1, op2});

        QCOMPARE(model.rowCount(), 2);
        const QModelIndex idx0 = model.index(0, 0);
        QCOMPARE(idx0.data(OperationListModel::DisplayTargetRole).toString(), QStringLiteral("Page 1"));
        QCOMPARE(idx0.data(OperationListModel::StageRole).value<OperationStage>(), OperationStage::Ocr);
        QCOMPARE(idx0.data(OperationListModel::StageNameRole).toString(), QStringLiteral("OCR 识别"));
        QCOMPARE(idx0.data(OperationListModel::StateRole).value<OperationState>(), OperationState::Running);
        QCOMPARE(idx0.data(OperationListModel::StateNameRole).toString(), QStringLiteral("处理中"));
        QCOMPARE(idx0.data(OperationListModel::ProgressPercentRole).toInt(), 40);
        QCOMPARE(idx0.data(OperationListModel::CancellableRole).toBool(), true);
        QCOMPARE(idx0.data(OperationListModel::IsActiveRole).toBool(), true);

        const QModelIndex idx1 = model.index(1, 0);
        QCOMPARE(idx1.data(OperationListModel::DisplayTargetRole).toString(), QStringLiteral("Page 2"));
        QCOMPARE(idx1.data(OperationListModel::StageRole).value<OperationStage>(), OperationStage::Translate);
        QCOMPARE(idx1.data(OperationListModel::StageNameRole).toString(), QStringLiteral("AI 翻译"));
        QCOMPARE(idx1.data(OperationListModel::StateRole).value<OperationState>(), OperationState::Queued);
        QCOMPARE(idx1.data(OperationListModel::StateNameRole).toString(), QStringLiteral("排队中"));
        QCOMPARE(idx1.data(OperationListModel::ProgressPercentRole).toInt(), 0);
    }

    void fineGrainedDataChangedOnProgressUpdate() {
        OperationListModel model;
        auto op1 =
            createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running, 10, 100);
        auto op2 =
            createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Running, 20, 100);
        auto op3 =
            createSampleSnapshot(QStringLiteral("Page 3"), OperationStage::Clear, OperationState::Queued, 0, 100);

        model.setOperations({op1, op2, op3});

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);

        // Update ONLY op2's progress
        op2.completed = 75;
        model.setOperations({op1, op2, op3});

        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(dataChangedSpy.count(), 1);

        const auto args = dataChangedSpy.takeFirst();
        const auto topLeft = args.at(0).toModelIndex();
        const auto bottomRight = args.at(1).toModelIndex();

        QCOMPARE(topLeft.row(), 1);
        QCOMPARE(bottomRight.row(), 1);
        QCOMPARE(model.index(1, 0).data(OperationListModel::ProgressPercentRole).toInt(), 75);
    }

    void insertAndRemoveEmitsRowSignals() {
        OperationListModel model;
        const auto op1 = createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running);
        model.setOperations({op1});

        QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        // Append op2
        const auto op2 =
            createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Queued);
        model.setOperations({op1, op2});

        QCOMPARE(insertSpy.count(), 1);
        QCOMPARE(removeSpy.count(), 0);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(model.rowCount(), 2);

        // Remove op1
        insertSpy.clear();
        model.setOperations({op2});

        QCOMPARE(insertSpy.count(), 0);
        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(OperationListModel::DisplayTargetRole).toString(), QStringLiteral("Page 2"));
    }

    void cancellableRoleRespectsStateAndDescriptor() {
        OperationListModel model;
        auto op1 = createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running);
        op1.descriptor.cancellable = true;

        auto op2 = createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Completed);
        op2.descriptor.cancellable = true;

        auto op3 = createSampleSnapshot(QStringLiteral("Page 3"), OperationStage::Clear, OperationState::Running);
        op3.descriptor.cancellable = false;

        model.setOperations({op1, op2, op3});

        QCOMPARE(model.index(0, 0).data(OperationListModel::CancellableRole).toBool(), true);
        QCOMPARE(model.index(1, 0).data(OperationListModel::CancellableRole).toBool(), false);
        QCOMPARE(model.index(2, 0).data(OperationListModel::CancellableRole).toBool(), false);
    }

    void removeMiddleRowEmitsRowSignals() {
        OperationListModel model;
        const auto op1 = createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running);
        const auto op2 =
            createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Queued);
        const auto op3 = createSampleSnapshot(QStringLiteral("Page 3"), OperationStage::Clear, OperationState::Queued);
        model.setOperations({op1, op2, op3});

        QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        // Remove middle row (op2)
        model.setOperations({op1, op3});

        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(0, 0).data(OperationListModel::DisplayTargetRole).toString(), QStringLiteral("Page 1"));
        QCOMPARE(model.index(1, 0).data(OperationListModel::DisplayTargetRole).toString(), QStringLiteral("Page 3"));
    }

    void errorChangeEmitsDataChanged() {
        OperationListModel model;
        auto op1 = createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running);
        model.setOperations({op1});

        QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        op1.state = OperationState::Failed;
        op1.error = makeApplicationError(ErrorCategory::Internal, "ocr.failed", "Failed to recognize text");
        model.setOperations({op1});

        QCOMPARE(dataChangedSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(model.index(0, 0).data(OperationListModel::ErrorMessageRole).toString(),
                 QStringLiteral("Failed to recognize text"));
    }

    void findOperationById() {
        OperationListModel model;
        const auto op1 = createSampleSnapshot(QStringLiteral("Page 1"), OperationStage::Ocr, OperationState::Running);
        const auto op2 =
            createSampleSnapshot(QStringLiteral("Page 2"), OperationStage::Translate, OperationState::Queued);

        model.setOperations({op1, op2});

        const auto found = model.findOperation(op2.id);
        QVERIFY(found.has_value());
        QCOMPARE(found->id, op2.id);
        QCOMPARE(found->descriptor.displayTarget, QStringLiteral("Page 2"));

        const auto notFound = model.findOperation(OperationId::generate());
        QVERIFY(!notFound.has_value());
    }
};

QTEST_MAIN(OperationListModelTest)
#include "operation_list_model_test.moc"
