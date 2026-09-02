#pragma once

#include <QObject>

class QtTextLayouterTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testBasicLayout();
    void testCancellation();
};
