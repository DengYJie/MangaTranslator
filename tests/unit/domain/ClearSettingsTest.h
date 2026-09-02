#pragma once

#include <QObject>

class ClearSettingsTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testDefaults();
    void testValidation();
    void testEquality();
};
