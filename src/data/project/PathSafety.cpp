#include "data/project/PathSafety.h"

#include <QDir>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace manga::data {

namespace {

// Converts between QString and std::filesystem paths using the native narrow or wide form.
[[nodiscard]] fs::path toFsPath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(path.toStdString());
#endif
}

[[nodiscard]] QString fromFsPath(const fs::path& path) {
#ifdef Q_OS_WIN
    QString result = QString::fromStdWString(path.wstring());
    // The Win32 resolver may return the \\?\ extended-length prefix.
    if (result.startsWith(QStringLiteral("\\\\?\\"))) {
        result.remove(0, 4);
    }
    return QDir::fromNativeSeparators(result);
#else
    return QString::fromStdString(path.string());
#endif
}

} // namespace

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

QString canonicalPathResolved(const QString& path) {
    std::error_code error;
    const fs::path resolved = fs::weakly_canonical(toFsPath(path), error);
    if (error) {
        return {};
    }
    return fromFsPath(resolved);
}

QString resolveInsideCanonicalRoot(const QString& canonicalRoot, const QString& relativePath) {
    if (!isSafeRelativeAssetPath(relativePath)) {
        return {};
    }
    const QString joined = canonicalRoot + QLatin1Char('/') + QDir::fromNativeSeparators(relativePath);
    if (!QFileInfo(joined).exists()) {
        return {};
    }
    const QString canonicalTarget = canonicalPathResolved(joined);
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
