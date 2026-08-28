#pragma once

#include <QObject>

class UuidTest : public QObject {
    Q_OBJECT

private slots:
    void testV4Generation();
    void testParseValid();
    void testParseInvalid();
    void testStringRoundTrip();
    void testComparisonAndHashing();
    void testStronglyTypedIds();
};
