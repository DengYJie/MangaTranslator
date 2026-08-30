#include "ui/screen/home/RelativeTime.h"

#include <QRegularExpression>
#include <QtTest>

class RelativeTimeTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void thresholds() {
        const qint64 now = 1'800'000'000'000LL;
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 30'000, now), QStringLiteral("刚刚"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 5 * 60'000, now), QStringLiteral("5 分钟前"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 3 * 3'600'000, now), QStringLiteral("3 小时前"));
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now - 4 * 86'400'000LL, now), QStringLiteral("4 天前"));
        const QString old = manga::ui::screen::home::formatRelativeTime(now - 60 * 86'400'000LL, now);
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$")).match(old).hasMatch());
    }

    void futureTimestampIsJustNow() {
        const qint64 now = 1'800'000'000'000LL;
        QCOMPARE(manga::ui::screen::home::formatRelativeTime(now + 10'000, now), QStringLiteral("刚刚"));
    }
};

QTEST_GUILESS_MAIN(RelativeTimeTest)
#include "relative_time_test.moc"
