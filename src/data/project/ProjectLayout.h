#pragma once

#include <QLatin1String>

namespace manga::data {

// Shared on-disk layout constants for the directory-based project format. Adapters must not invent
// their own staging or asset locations, and every name below is how an existing project directory
// or its leftovers are recognized on disk, so a rename strands those files rather than migrating
// them. The staging directory lives at the project root, on the same volume as the asset targets,
// so atomic renames stay possible and leftover staging files are never mistaken for project
// content inside assets/.
//
// project.json is the document a save replaces. Its .bak sibling is a single-generation copy of
// the last complete and valid save, and project open reads it back only when project.json itself
// fails to validate.
//
// pending-batch.json is the crash marker of an unacknowledged import batch. The importer writes the
// keys batchId, projectId and assetPaths, where assetPaths lists project-relative originals under
// the assets/original prefix, and project open reads assetPaths alone while skipping any entry
// outside that prefix. Renaming the marker, renaming assetPaths, or moving the original asset
// prefix leaves orphaned originals on disk with no diagnostic.
inline constexpr QLatin1String kProjectFileName("project.json");
inline constexpr QLatin1String kBackupSuffix(".bak");
inline constexpr QLatin1String kLockFileName(".mangatranslator.lock");
inline constexpr QLatin1String kStagingDirName(".mangatranslator-staging");
inline constexpr QLatin1String kPendingBatchFileName("pending-batch.json");
inline constexpr QLatin1String kAssetsOriginal("assets/original");
inline constexpr QLatin1String kAssetsDerived("assets/derived");

} // namespace manga::data
