#pragma once

#include <QObject>

class SettingsCredentialUseCasesTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void loadsDefaultSettingsWhenStoreEmpty();
    void loadsDefaultSettingsWhenStoreNull();
    void savesAndLoadsValidSnapshotRoundtrip();
    void coercesUnknownThemeValuesToDefaults();
    void clampsOutOfRangePersistedNumbers();
    void keepsDefaultsForUnreadablePersistedValues();
    void rejectsInvalidEndpointAndWritesNothing();
    void rejectsInvalidTimeoutAndWritesNothing();
    void rejectsInvalidRetryCountAndWritesNothing();
    void allowsEmptyModelDirectory();
    void writtenKeysDoNotContainApiKeyOrSecret();
    void rejectsSaveWithoutStoreAndWritesNothing();

    void testCheckCredentialStatus();
    void testSetCredential();
    void testClearCredential();
    void testValidation();
};
