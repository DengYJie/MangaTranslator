#include "rendering/FontRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>

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
            QDir::currentPath() + QStringLiteral("/fonts"),
            QStringLiteral("F:/source/MangaTranslator/res/fonts")};
        for (const auto& dirPath : searchDirs) {
            QDir dir(dirPath);
            if (!dir.exists()) {
                continue;
            }
            const auto fontFiles =
                dir.entryList({QStringLiteral("*.otf"), QStringLiteral("*.ttf"), QStringLiteral("*.ttc")}, QDir::Files);
            for (const auto& file : fontFiles) {
                const int id = QFontDatabase::addApplicationFont(dir.filePath(file));
                if (id != -1) {
                    const QStringList families = QFontDatabase::applicationFontFamilies(id);
                    for (const auto& f : families) {
                        if (!result.contains(f)) {
                            result.append(f);
                        }
                    }
                }
            }
        }
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
