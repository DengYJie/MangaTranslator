#pragma once

#include <QObject>

class TextLayoutTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaults();
    void testValidation();
    void testOrientationAndAlignment();
    void testEquality();
};
