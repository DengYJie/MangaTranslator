#pragma once

#include <QObject>

class SettingsCredentialUseCasesTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void loadsDefaultSettingsWhenStoreEmpty();
    void loadsDefaultSettingsWhenStoreNull();
    void savesAndLoadsValidSnapshotRoundtrip();
    void coercesUnknownThemeAndDensityValuesToDefaults();
    void clampsOutOfRangePersistedNumbers();
    void keepsDefaultsForUnreadablePersistedValues();
    void readsTextFormsOfBooleanSettings();
    void rejectsInvalidEndpointAndWritesNothing();
    void rejectsInvalidTimeoutAndWritesNothing();
    void rejectsInvalidRetryCountAndWritesNothing();
    void rejectsInvalidRecentLimitAndWritesNothing();
    void rejectsInvalidCanvasBackgroundAndWritesNothing();
    void rejectsEmptyOcrLanguageAndWritesNothing();
    void rejectsEmptyProviderIdAndWritesNothing();
    void allowsEmptyModelDirectory();
    void writtenKeysDoNotContainApiKeyOrSecret();
    void rejectsSaveWithoutStoreAndWritesNothing();

    void testCheckCredentialStatus();
    void testSetCredential();
    void testClearCredential();
    void testValidation();
};
