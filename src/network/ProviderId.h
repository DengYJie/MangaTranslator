#pragma once

#include <QLatin1String>
#include <QRegularExpression>
#include <QString>

namespace manga::network {

inline constexpr QLatin1String kProviderIdPattern("^[a-z0-9][a-z0-9._-]{0,63}$");
inline constexpr QLatin1String kProviderIdPatternDisplay("[a-z0-9][a-z0-9._-]{0,63}");

/// Stable lowercase provider identity used by configuration, credential targets and logs.
[[nodiscard]] inline bool isValidProviderId(const QString& providerId) {
    static const QRegularExpression pattern(kProviderIdPattern);
    return pattern.match(providerId).hasMatch();
}

/// Credential Manager target for one provider. The provider id is validated first, so a
/// caller can never steer the target outside the application namespace.
[[nodiscard]] inline QString credentialTargetName(const QString& providerId) {
    return QStringLiteral("MangaTranslator/Credential/%1/ApiKey").arg(providerId);
}

} // namespace manga::network
