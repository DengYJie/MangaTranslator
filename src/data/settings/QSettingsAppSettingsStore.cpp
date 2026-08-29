#include "QSettingsAppSettingsStore.h"

#include <QSettings>

namespace manga::data {

std::optional<QVariant> QSettingsAppSettingsStore::readSetting(const QString& key) const {
    QSettings settings;
    if (!settings.contains(key)) {
        return std::nullopt;
    }
    return settings.value(key);
}

void QSettingsAppSettingsStore::writeSetting(const QString& key, const QVariant& value) {
    QSettings settings;
    settings.setValue(key, value);
}

void QSettingsAppSettingsStore::removeSetting(const QString& key) {
    QSettings settings;
    settings.remove(key);
}

} // namespace manga::data
