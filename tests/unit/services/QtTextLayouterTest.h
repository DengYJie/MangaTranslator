#pragma once

#include <QObject>

class QtTextLayouterTest : public QObject {
    Q_OBJECT

private slots:
    void testBasicLayout();
    void testCancellation();
};
