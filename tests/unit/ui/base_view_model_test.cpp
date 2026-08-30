#include "ui/base/BaseViewModel.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

struct CounterState {
    int value{0};
    QString label;
    bool operator==(const CounterState&) const = default;
};

class CounterViewModel : public manga::ui::base::BaseViewModel<CounterViewModel, CounterState> {
    Q_OBJECT
public:
    using BaseViewModel::BaseViewModel;
    void setValue(int value) {
        updateState([value](CounterState& state) { state.value = value; });
    }
    void replace(const CounterState& next) {
        updateState(next);
    }
    quint64 begin() {
        return beginRequest();
    }
    using BaseViewModel::invalidateRequests;
    using BaseViewModel::isRequestCurrent;
signals:
    void stateChanged(const CounterState& state);

protected:
    void emitStateChanged() override {
        Q_EMIT stateChanged(m_state);
    }
};

class Probe : public QObject {
    Q_OBJECT
public:
    int replayCount{0};
    CounterState last;
public slots:
    void render(const CounterState& state) {
        ++replayCount;
        last = state;
    }
};

} // namespace

class BaseViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void observeReplaysCurrentStateImmediately() {
        CounterViewModel viewModel;
        viewModel.setValue(7);
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        QCOMPARE(probe.replayCount, 1);
        QCOMPARE(probe.last.value, 7);
    }

    void equalStateIsSuppressed() {
        CounterViewModel viewModel;
        QSignalSpy spy(&viewModel, &CounterViewModel::stateChanged);
        viewModel.setValue(1);
        viewModel.setValue(1); // equal, suppressed
        viewModel.replace(CounterState{});
        viewModel.replace(CounterState{});
        QCOMPARE(spy.count(), 2);
    }

    void partialAndFullUpdateSequence() {
        CounterViewModel viewModel;
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        viewModel.setValue(3);
        CounterState next;
        next.value = 9;
        next.label = QStringLiteral("done");
        viewModel.replace(next);
        QCOMPARE(probe.replayCount, 3); // replay plus two changes
        QCOMPARE(probe.last.value, 9);
        QCOMPARE(probe.last.label, QStringLiteral("done"));
    }

    void requestTokensInvalidateStaleCompletions() {
        CounterViewModel viewModel;
        const quint64 first = viewModel.begin();
        const quint64 second = viewModel.begin();
        QVERIFY(!viewModel.isRequestCurrent(first));
        QVERIFY(viewModel.isRequestCurrent(second));
        viewModel.invalidateRequests();
        QVERIFY(!viewModel.isRequestCurrent(second));
    }

    void observeTwiceDoesNotDuplicateDelivery() {
        CounterViewModel viewModel;
        Probe probe;
        viewModel.observe(&probe, &Probe::render);
        viewModel.observe(&probe, &Probe::render); // UniqueConnection dedupes, each observe replays once
        QCOMPARE(probe.replayCount, 2);
        viewModel.setValue(5);
        QCOMPARE(probe.replayCount, 3);
    }
};

QTEST_MAIN(BaseViewModelTest)
#include "base_view_model_test.moc"
