#pragma once

#include "application/operation/OperationContext.h"
#include "domain/model/Uuid.h"

#include <QHash>
#include <QMutex>

#include <memory>
#include <stop_token>

namespace manga::application {

struct InFlightOperation {
    OperationId id;
    std::stop_source stopSource;
};

class OperationRegistry {
public:
    OperationRegistry() : m_state(std::make_shared<State>()) {}

    [[nodiscard]] OperationContext createOperation(quint64 generation) {
        InFlightOperation operation;
        operation.id = OperationId::generate();

        const auto key = QString::fromStdString(operation.id.toString());
        {
            QMutexLocker locker(&m_state->mutex);
            m_state->operations.insert(key, operation);
        }

        OperationContext context;
        context.operationId = operation.id;
        context.generation = generation;
        context.stopToken = operation.stopSource.get_token();
        context.lifetime = std::shared_ptr<void>(new char, [state = m_state, key](void* value) {
            delete static_cast<char*>(value);
            QMutexLocker locker(&state->mutex);
            state->operations.remove(key);
        });
        return context;
    }

    void cancelOperation(OperationId id) {
        const auto key = QString::fromStdString(id.toString());
        QMutexLocker locker(&m_state->mutex);
        auto it = m_state->operations.find(key);
        if (it != m_state->operations.end()) {
            it.value().stopSource.request_stop();
            m_state->operations.erase(it);
        }
    }

    [[nodiscard]] qsizetype inFlightCount() const {
        QMutexLocker locker(&m_state->mutex);
        return m_state->operations.size();
    }

private:
    struct State {
        mutable QMutex mutex;
        QHash<QString, InFlightOperation> operations;
    };

    std::shared_ptr<State> m_state;
};

} // namespace manga::application
