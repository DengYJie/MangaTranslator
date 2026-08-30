#pragma once

#include <QString>
#include <QtGlobal>

namespace manga::ui::screen::home {

/// Formats a timestamp into a human-readable relative time string:
/// "刚刚" / "N 分钟前" / "N 小时前" / "N 天前" / "yyyy-MM-dd" for timestamps >= 30 days.
QString formatRelativeTime(qint64 lastOpenedMs, qint64 nowMs);

} // namespace manga::ui::screen::home
