#include "rendering/FontRegistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSet>

namespace manga::rendering {

QStringList FontRegistry::bundledFontFamilies() {
    static const QStringList bundled = []() {
        QStringList result;
        const QStringList searchDirs = {
#ifdef MANGA_RES_FONTS_DIR
            QStringLiteral(MANGA_RES_FONTS_DIR),
#endif
            QStringLiteral(":/manga_rendering/fonts"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/fonts"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/res/fonts"),
            QDir::currentPath() + QStringLiteral("/res/fonts"),
            QDir::currentPath() + QStringLiteral("/fonts")};

        // In a source checkout several of these entries resolve to the same directory, and
        // addApplicationFont parses the whole file on every call, so a repeat costs a full font
        // load rather than a lookup. Deduplicating by canonical path keeps the family list and the
        // registration count in step.
        QSet<QString> registeredPaths;
        for (const auto& dirPath : searchDirs) {
            QDir dir(dirPath);
            if (!dir.exists()) {
                continue;
            }
            const auto fontFiles =
                dir.entryList({QStringLiteral("*.otf"), QStringLiteral("*.ttf"), QStringLiteral("*.ttc")}, QDir::Files);
            for (const auto& file : fontFiles) {
                const QString filePath = dir.filePath(file);
                const QString canonical = QFileInfo(filePath).canonicalFilePath();
                const QString key = canonical.isEmpty() ? QDir::cleanPath(filePath) : canonical;
                if (registeredPaths.contains(key)) {
                    continue;
                }
                registeredPaths.insert(key);
                const int id = QFontDatabase::addApplicationFont(filePath);
                if (id != -1) {
                    const QStringList families = QFontDatabase::applicationFontFamilies(id);
                    qInfo().noquote() << "[FontRegistry] Loaded font file:" << filePath << "id=" << id
                                      << "families=" << families;
                    for (const auto& f : families) {
                        if (!result.contains(f)) {
                            result.append(f);
                        }
                    }
                } else {
                    qWarning().noquote() << "[FontRegistry] Failed to load font file:" << filePath;
                }
            }
        }
        qInfo().noquote() << "[FontRegistry] Total bundled font families registered:" << result;
        return result;
    }();
    return bundled;
}

QStringList FontRegistry::availableFontFamilies() {
    // Only return bundled/embedded fonts, do not load system fonts.
    return bundledFontFamilies();
}

QString FontRegistry::defaultFontFamily() {
    const QStringList bundled = bundledFontFamilies();
    if (!bundled.isEmpty()) {
        for (const auto& name : bundled) {
            if (name.contains(QStringLiteral("SC"), Qt::CaseInsensitive) ||
                name.contains(QStringLiteral("Simplified"), Qt::CaseInsensitive)) {
                return name;
            }
        }
        return bundled.first();
    }
    return QStringLiteral("Noto Sans CJK SC");
}

} // namespace manga::rendering
