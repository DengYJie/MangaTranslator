#pragma once

#include <QString>
#include <QStringList>

namespace manga::rendering {

/// Centralized registry for discovering and querying bundled and available fonts.
class FontRegistry {
public:
    /// Ensures bundled font files (NotoSansCJK-VF, etc.) are discovered and registered
    /// with QFontDatabase. Returns the list of family names provided by the bundled font files.
    static QStringList bundledFontFamilies();

    /// Returns all available font families (bundled fonts at top, followed by unique system fonts).
    static QStringList availableFontFamilies();

    /// Returns the default font family name (preferred CJK font or standard fallback).
    static QString defaultFontFamily();
};

} // namespace manga::rendering
