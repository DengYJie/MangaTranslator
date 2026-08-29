#pragma once

#include <QString>
#include <QVariant>

#include <optional>

namespace manga::application {

/// Narrow port for application-wide settings, abstracting away QSettings or JSON config files.
/// This prevents UI and ViewModels from depending directly on infrastructure-specific storage.
class IAppSettingsStore {
public:
    virtual ~IAppSettingsStore() = default;

    /// Reads a setting by key. Returns std::nullopt if the setting is not found.
    [[nodiscard]] virtual std::optional<QVariant> readSetting(const QString& key) const = 0;

    /// Writes a setting by key.
    virtual void writeSetting(const QString& key, const QVariant& value) = 0;

    /// Removes a setting by key.
    virtual void removeSetting(const QString& key) = 0;
};

} // namespace manga::application
