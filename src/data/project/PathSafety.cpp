#include "data/project/PathSafety.h"

#include <QDir>
#include <QFileInfo>

namespace manga::data {

bool isSafeRelativeAssetPath(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    const QString normalized = QDir::fromNativeSeparators(path);
    if (QDir::isAbsolutePath(normalized)) {
        return false;
    }
    const QStringList segments = normalized.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
        return false;
    }
    for (const QString& segment : segments) {
        if (segment == QLatin1String("..") || segment == QLatin1String(".")) {
            return false;
        }
    }
    return true;
}

QString resolveInsideCanonicalRoot(const QString& canonicalRoot, const QString& relativePath) {
    if (!isSafeRelativeAssetPath(relativePath)) {
        return {};
    }
    const QString joined = canonicalRoot + QLatin1Char('/') + QDir::fromNativeSeparators(relativePath);
    const QFileInfo info(joined);
    if (!info.exists()) {
        return {};
    }
    const QString canonicalTarget = info.canonicalFilePath();
    if (canonicalTarget.isEmpty() || !isInsideCanonicalRoot(canonicalRoot, canonicalTarget)) {
        return {};
    }
    return canonicalTarget;
}

QString canonicalDirectoryPath(const QString& absolutePath) {
    const QFileInfo info(QDir::cleanPath(absolutePath));
    if (!info.exists() || !info.isDir()) {
        return {};
    }
    return info.canonicalFilePath();
}

QString canonicalOrCleanedPath(const QString& absolutePath) {
    const QString canonical = canonicalDirectoryPath(absolutePath);
    if (!canonical.isEmpty()) {
        return canonical;
    }
    return QDir::cleanPath(absolutePath);
}

bool isInsideCanonicalRoot(const QString& canonicalRoot, const QString& candidatePath) {
    if (candidatePath == canonicalRoot) {
        return true;
    }
    return candidatePath.startsWith(canonicalRoot + QLatin1Char('/'));
}

} // namespace manga::data
