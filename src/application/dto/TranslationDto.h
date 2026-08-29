#pragma once

#include "domain/model/GlossaryEntry.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/ProjectSettings.h"
#include "domain/model/ReadingDirection.h"
#include "domain/model/Uuid.h"

#include <QString>
#include <QVector>

namespace manga::application {

struct TranslationRegion {
    domain::RegionId regionId;
    QString sourceText;
};

struct TranslationRequest {
    domain::LanguageCode sourceLanguage;
    domain::LanguageCode targetLanguage;
    domain::ReadingDirection readingDirection;
    QVector<TranslationRegion> regions;
    QVector<domain::GlossaryEntry> glossary;
};

struct TranslationResponseItem {
    domain::RegionId regionId;
    QString sourceText;
    QString translation;
    QVector<QString> candidates;
};

struct TranslationResponse {
    // Identity of the provider configuration that produced this response; model candidates
    // reference it instead of a hardcoded translator name.
    QString providerId;
    QVector<TranslationResponseItem> items;
};

} // namespace manga::application
