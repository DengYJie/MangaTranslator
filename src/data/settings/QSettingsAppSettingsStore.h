#pragma once

#include "application/ports/IAppSettingsStore.h"

namespace manga::data {

/// QSettings implementation of the application settings port. It stores each caller-supplied key
/// verbatim under the application and organization names set at startup, so the persisted spelling
/// of a key belongs to the code that passes it in. Nothing migrates: a renamed key reads back as
/// absent and the caller falls back to its own default.
class QSettingsAppSettingsStore final : public application::IAppSettingsStore {
public:
    QSettingsAppSettingsStore() = default;

    [[nodiscard]] std::optional<QVariant> readSetting(const QString& key) const override;
    void writeSetting(const QString& key, const QVariant& value) override;
    void removeSetting(const QString& key) override;
};

} // namespace manga::data
