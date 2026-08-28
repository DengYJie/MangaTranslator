#pragma once

#include <QString>

#include <expected>
#include <utility>

namespace manga::application {

enum class ErrorCategory {
    Input,
    Project,
    Model,
    Network,
    Protocol,
    Inference,
    Persistence,
    Export,
    Cancelled,
    Internal
};

enum class RecoveryAction {
    None,
    Retry,
    OpenSettings,
    LocateModel,
    ChoosePath,
    Reauthenticate,
    FreeDiskSpace
};

struct ApplicationError {
    ErrorCategory category{ErrorCategory::Internal};
    QString code;
    QString safeMessage;
    QString diagnostic;
    RecoveryAction recovery{RecoveryAction::None};
    bool retryable{false};

    [[nodiscard]] bool isCancelled() const noexcept {
        return category == ErrorCategory::Cancelled;
    }
};

[[nodiscard]] inline ApplicationError makeApplicationError(ErrorCategory category,
                                                           QString code,
                                                           QString safeMessage,
                                                           RecoveryAction recovery = RecoveryAction::None,
                                                           bool retryable = false) {
    return ApplicationError{category, std::move(code), std::move(safeMessage), {}, recovery, retryable};
}

template <class T> using Result = std::expected<T, ApplicationError>;

} // namespace manga::application
