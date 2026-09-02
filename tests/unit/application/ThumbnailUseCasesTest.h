#pragma once

#include <QtTest/QtTest>

class ThumbnailUseCasesTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testLoadExistingThumbnail();
    void testGenerateAndSaveThumbnail();
    void testGenerateFailsIfLoaderFails();
    void testValidation();
};
