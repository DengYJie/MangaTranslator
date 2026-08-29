#pragma once

#include "application/ports/IAppSettingsStore.h"

namespace manga::data {

class QSettingsAppSettingsStore final : public application::IAppSettingsStore {
public:
    QSettingsAppSettingsStore() = default;

    [[nodiscard]] std::optional<QVariant> readSetting(const QString& key) const override;
    void writeSetting(const QString& key, const QVariant& value) override;
    void removeSetting(const QString& key) override;
};

} // namespace manga::data
