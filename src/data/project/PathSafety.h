#pragma once

#include <QString>

namespace manga::data {

/// True when path is a nonempty project-relative path with forward separators and no absolute
/// prefix, drive letter, or ".." segment anywhere.
[[nodiscard]] bool isSafeRelativeAssetPath(const QString& path);

/// Resolves a validated relative path under a canonical project root and verifies that the
/// target stays inside the root after resolving symlinks, junctions, and reparse points.
/// Returns an empty string when the path escapes the root or the target does not exist.
[[nodiscard]] QString resolveInsideCanonicalRoot(const QString& canonicalRoot, const QString& relativePath);

/// Fully resolves a path through symlinks, junctions, and reparse points. Returns an empty
/// string when the path cannot be resolved. QFileInfo::canonicalFilePath does not resolve
/// NTFS junctions, so containment checks must use this function.
[[nodiscard]] QString canonicalPathResolved(const QString& path);

/// Returns the canonical filesystem path of an existing directory, or an empty string when the
/// path does not exist or is not a directory.
[[nodiscard]] QString canonicalDirectoryPath(const QString& absolutePath);

/// Returns the canonical path of an existing directory, or the cleaned absolute path for a
/// not-yet-existing directory, so callers can key runtime state before creation.
[[nodiscard]] QString canonicalOrCleanedPath(const QString& absolutePath);

/// True when candidate is canonicalRoot itself or a descendant of it (string prefix on the
/// canonical forms with a separator boundary).
[[nodiscard]] bool isInsideCanonicalRoot(const QString& canonicalRoot, const QString& candidatePath);

} // namespace manga::data
