#include "ClearSettingsTest.h"

#include "domain/model/ClearSettings.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void ClearSettingsTest::testDefaults() {
    const ClearSettings clear;
    QVERIFY(clear.isValid());
    QCOMPARE(clear.enabled, true);
    QCOMPARE(QString::fromStdString(clear.colorHex), "#FFFFFF");
    QCOMPARE(clear.opacity, 1.0);
}

void ClearSettingsTest::testValidation() {
    ClearSettings clear;
    clear.opacity = 0.5;
    QVERIFY(clear.isValid());

    clear.opacity = 0.0;
    QVERIFY(clear.isValid());

    clear.opacity = 1.0;
    QVERIFY(clear.isValid());

    clear.opacity = -0.1;
    QVERIFY(!clear.isValid());

    clear.opacity = 1.01;
    QVERIFY(!clear.isValid());

    clear.opacity = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!clear.isValid());
}

void ClearSettingsTest::testEquality() {
    const ClearSettings c1;
    ClearSettings c2;
    QVERIFY(c1 == c2);

    c2.opacity = 0.8;
    QVERIFY(c1 != c2);
}
