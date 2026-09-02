#pragma once

#include <QObject>

class TextLayoutTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testDefaults();
    void testValidation();
    void testOrientationAndAlignment();
    void testEquality();
};
