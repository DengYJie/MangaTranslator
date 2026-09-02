#include "ui/base/BasePage.h"
#include "ui/base/BaseViewModel.h"

#include <QLineEdit>
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
Q_SIGNALS:
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
public Q_SLOTS:
    void render(const CounterState& state) {
        ++replayCount;
        last = state;
    }
};

class ProbePage : public manga::ui::base::BasePage {
    Q_OBJECT
public:
    using BasePage::BasePage;
    QVector<int> responsiveWidths;
    int themeUpdates{0};
    void onThemeUpdated() override {
        manga::ui::base::BasePage::onThemeUpdated();
        ++themeUpdates;
    }

protected:
    void updateResponsiveLayout(int availableWidth) override {
        responsiveWidths.append(availableWidth);
    }
};

} // namespace

class BaseMvvmTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
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

    void resizeTriggersResponsiveHook() {
        ProbePage page;
        page.resize(800, 600);
        // Hidden widgets defer the resize event; show flushes the pending one.
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(page.responsiveWidths.contains(800), 2000);
        page.resize(500, 400);
        QTRY_VERIFY_WITH_TIMEOUT(page.responsiveWidths.contains(500), 2000);
    }

    void showFocusesDefaultWidget() {
        ProbePage page;
        auto* edit = new QLineEdit(&page);
        page.setDefaultFocusWidget(edit);
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(edit->hasFocus(), 2000);
    }

    void noDefaultFocusIsHarmless() {
        ProbePage page;
        page.show(); // no default focus set; must not crash
        QVERIFY(page.defaultFocusWidget() == nullptr);
    }
};

QTEST_MAIN(BaseMvvmTest)
#include "BaseMvvmTest.moc"
