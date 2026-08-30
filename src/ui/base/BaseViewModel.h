#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <atomic>
#include <functional>
#include <type_traits>
#include <utility>

namespace manga::ui::base {

/// CRTP ViewModel base implementing unidirectional data flow.
///
/// Contract:
/// - State holds persistent UI data as a value type with deterministic equality.
/// - One-shot events (toast, dialog, navigation) must be separate Qt signals, never State.
/// - All methods must be called on the GUI thread.
template <typename Derived, typename State> class BaseViewModel : public QObject {
public:
    explicit BaseViewModel(QObject* parent = nullptr) : QObject(parent) {}
    ~BaseViewModel() override = default;

    /// Current immutable state snapshot.
    [[nodiscard]] const State& state() const {
        return m_state;
    }

    /// Starts a new request: bumps the token so all older completions become stale.
    quint64 beginRequest() {
        return ++m_requestId;
    }

    /// Returns false when the completion carrying `requestId` is stale.
    [[nodiscard]] bool isRequestCurrent(quint64 requestId) const {
        return requestId == m_requestId;
    }

    /// Invalidates every in-flight request.
    void invalidateRequests() {
        ++m_requestId;
    }

    /// Subscribes `slot` to state changes with an immediate replay of the current state.
    /// A newly attached observer never waits for the next mutation.
    template <typename Context, typename Slot> void observe(Context* context, Slot slot) {
        Q_ASSERT_X(
            QThread::currentThread() == qApp->thread(), "BaseViewModel", "observe must be called on the GUI thread");
        connect(static_cast<Derived*>(this),
                &Derived::stateChanged,
                context,
                slot,
                static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));
        if constexpr (std::is_member_function_pointer_v<Slot>) {
            std::invoke(slot, context, m_state);
        } else {
            std::invoke(slot, m_state);
        }
    }

protected:
    State m_state;

    /// Applies a partial update; emits stateChanged only when the snapshot actually changed.
    void updateState(const std::function<void(State&)>& updater) {
        Q_ASSERT_X(QThread::currentThread() == qApp->thread(),
                   "BaseViewModel",
                   "updateState must be called on the GUI thread");
        State next = m_state;
        updater(next);
        if (next == m_state) {
            return;
        }
        m_state = std::move(next);
        emitStateChanged();
    }

    /// Replaces the whole snapshot with the same equality suppression.
    void updateState(const State& newState) {
        Q_ASSERT_X(QThread::currentThread() == qApp->thread(),
                   "BaseViewModel",
                   "updateState must be called on the GUI thread");
        if (newState == m_state) {
            return;
        }
        m_state = newState;
        emitStateChanged();
    }

    /// Subclasses emit their own `stateChanged(const State&)` here.
    virtual void emitStateChanged() = 0;

private:
    std::atomic<quint64> m_requestId{0};
};

} // namespace manga::ui::base
