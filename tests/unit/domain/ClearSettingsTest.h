#pragma once

#include <QObject>

class ClearSettingsTest : public QObject {
    Q_OBJECT

private slots:
    void testDefaults();
    void testValidation();
    void testEquality();
};
