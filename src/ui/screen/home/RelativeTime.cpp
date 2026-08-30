#include "ui/screen/home/RelativeTime.h"

#include <QDateTime>

namespace manga::ui::screen::home {

QString formatRelativeTime(qint64 lastOpenedMs, qint64 nowMs) {
    const qint64 deltaMs = qMax<qint64>(0, nowMs - lastOpenedMs);
    constexpr qint64 kMinuteMs = 60'000;
    constexpr qint64 kHourMs = 3'600'000;
    constexpr qint64 kDayMs = 86'400'000;

    if (deltaMs < kMinuteMs) {
        return QStringLiteral("刚刚");
    }
    if (deltaMs < kHourMs) {
        return QStringLiteral("%1 分钟前").arg(deltaMs / kMinuteMs);
    }
    if (deltaMs < kDayMs) {
        return QStringLiteral("%1 小时前").arg(deltaMs / kHourMs);
    }
    if (deltaMs < 30 * kDayMs) {
        return QStringLiteral("%1 天前").arg(deltaMs / kDayMs);
    }
    return QDateTime::fromMSecsSinceEpoch(lastOpenedMs).toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace manga::ui::screen::home
