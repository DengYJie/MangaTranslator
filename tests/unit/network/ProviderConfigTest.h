#pragma once

#include <QObject>

class ProviderConfigTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void acceptsHttpsEndpoint();
    void rejectsPlainHttpByDefault();
    void allowsLoopbackHttpOnlyWhenOptedIn();
    void rejectsUserInfoAndFragment();
    void rejectsInvalidProviderId();
    void rejectsOutOfRangeValues();
};
