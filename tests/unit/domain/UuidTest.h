#pragma once

#include <QObject>

class UuidTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testV4Generation();
    void testParseValid();
    void testParseInvalid();
    void testStringRoundTrip();
    void testComparisonAndHashing();
    void testStronglyTypedIds();
};
