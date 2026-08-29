#pragma once

#include <QLatin1String>

namespace manga::data {

// Shared on-disk layout constants for the directory-based project format; adapters must not
// invent their own staging or asset locations. The staging directory lives at the project
// root (same volume as the asset targets) so atomic renames stay possible and leftover
// staging files are never mistaken for project content inside assets/.
inline constexpr QLatin1String kProjectFileName("project.json");
inline constexpr QLatin1String kBackupSuffix(".bak");
inline constexpr QLatin1String kLockFileName(".mangatranslator.lock");
inline constexpr QLatin1String kStagingDirName(".mangatranslator-staging");
inline constexpr QLatin1String kAssetsOriginal("assets/original");
inline constexpr QLatin1String kAssetsDerived("assets/derived");

} // namespace manga::data
