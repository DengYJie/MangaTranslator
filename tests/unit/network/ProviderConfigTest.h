#pragma once

#include <QObject>

class ProviderConfigTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsHttpsEndpoint();
    void rejectsPlainHttpByDefault();
    void allowsLoopbackHttpOnlyWhenOptedIn();
    void rejectsUserInfoAndFragment();
    void rejectsInvalidProviderId();
    void rejectsOutOfRangeValues();
};
