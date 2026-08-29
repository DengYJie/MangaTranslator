#pragma once

#include <QtTest/QtTest>

class ThumbnailUseCasesTest : public QObject {
    Q_OBJECT
private slots:
    void testLoadExistingThumbnail();
    void testGenerateAndSaveThumbnail();
    void testGenerateFailsIfLoaderFails();
    void testValidation();
};
