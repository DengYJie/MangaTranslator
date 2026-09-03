#pragma once

#include "application/error/ApplicationError.h"
#include "domain/model/ReadingDirection.h"

#include <QStandardPaths>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::ui::screen::import_ {

/// Returns the default project save directory: Documents/MangaTranslator.
/// The directory is not created here; the importer creates it on first use.
inline QString defaultSaveLocation() {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/MangaTranslator");
}

/// One top-level source row: an image file or an image folder.
struct ImportSourceEntry {
    QString path;
    QString displayName;
    bool isFolder{false};
    bool supported{true};
    QString errorSummary;
    bool operator==(const ImportSourceEntry&) const = default;
};

struct ImportNotice {
    enum class Severity {
        Info,
        Error
    };
    Severity severity{Severity::Info};
    QString message;
    QString actionText;
    application::RecoveryAction action{application::RecoveryAction::None};
    int occurrenceCount{1};
    bool operator==(const ImportNotice&) const = default;
};

struct ImportState {
    QVector<ImportSourceEntry> sources;
    QString projectName;
    QString saveLocation{defaultSaveLocation()};
    domain::ReadingDirection readingDirection{domain::ReadingDirection::RightToLeft};
    QString sourceLanguageCode{QStringLiteral("ja")};
    QString targetLanguageCode{QStringLiteral("zh-Hans")};
    bool languageCaution{false}; // source == target: caution only, still allowed
    QString nameError;
    QString locationError;
    int formErrorCount{0}; // drives the disabled submit summary
    bool importInProgress{false};
    int importedPageCount{0};
    int totalPageCount{0};
    std::optional<ImportNotice> notice;
    bool operator==(const ImportState&) const = default;
};

} // namespace manga::ui::screen::import_
