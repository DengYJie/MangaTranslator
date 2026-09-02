#pragma once

#include <QString>
#include <QVariant>

#include <optional>

namespace manga::application {

/// Narrow port for application-wide settings, abstracting away QSettings or JSON config files.
/// This prevents UI and ViewModels from depending directly on infrastructure-specific storage.
/// Every operation runs synchronously on the caller's thread against the local store, and the two
/// mutators return nothing, so a rejected write cannot be observed by the caller.
class IAppSettingsStore {
public:
    virtual ~IAppSettingsStore() = default;

    /// Returns the value stored under key, or nothing when no value is stored under it.
    [[nodiscard]] virtual std::optional<QVariant> readSetting(const QString& key) const = 0;

    /// Replaces any value already stored under key, visible to the next readSetting of that key.
    virtual void writeSetting(const QString& key, const QVariant& value) = 0;

    /// Forgets the value stored under key so a later read returns nothing. Removing a key that was
    /// never stored is not an error.
    virtual void removeSetting(const QString& key) = 0;
};

} // namespace manga::application
