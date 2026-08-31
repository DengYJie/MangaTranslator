#pragma once

#include "application/error/ApplicationError.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>

namespace manga::ui::error {

/// Value-typed presentation of an error and its executable recovery action.
struct RecoveryPresentation {
    QString code;
    QString safeMessage;
    QString actionText;
    application::RecoveryAction action{application::RecoveryAction::None};
    QString targetKey;
    int occurrenceCount{1};
    bool blocking{false};

    bool operator==(const RecoveryPresentation&) const = default;
};

/// Translates domain/application errors into localized UI presentations,
/// throttles and deduplicates repeated errors within a configurable time window,
/// and maps RecoveryActions to executable UI routes.
class RecoveryPresenter {
public:
    explicit RecoveryPresenter(qint64 deduplicationWindowMs = 3000);

    /// Presents an ApplicationError with an optional targetKey and nowMs (timestamp in ms).
    /// If nowMs == 0, QDateTime::currentMSecsSinceEpoch() is used.
    /// Returns std::nullopt if the error represents a cancellation or stale request.
    /// Deduplicates identical (code + targetKey) occurrences within the time window.
    [[nodiscard]] std::optional<RecoveryPresentation>
    present(const application::ApplicationError& error, const QString& targetKey = QString(), qint64 nowMs = 0);

    /// Clears the deduplication history.
    void reset();

    /// Returns the localized safe message for the error code, or falls back to error.safeMessage.
    [[nodiscard]] static QString localizedSafeMessage(const application::ApplicationError& error);

    /// Returns the localized action button text for the RecoveryAction.
    [[nodiscard]] static QString localizedActionText(application::RecoveryAction action);

    /// Returns the suggested settings route for a given RecoveryAction / error code.
    [[nodiscard]] static QString settingsRouteForAction(application::RecoveryAction action,
                                                        const QString& errorCode = QString());

private:
    struct DeduplicationEntry {
        qint64 lastSeenMs{0};
        int count{0};
    };

    qint64 m_windowMs{3000};
    QHash<QString, DeduplicationEntry> m_history;
};

} // namespace manga::ui::error
