#pragma once

#include "application/error/ApplicationError.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector>

#include <array>

namespace manga::services {

struct OcrGraphManifest {
    QString role;
    QString file;
    QString sha256;
    QStringList inputs;
    QStringList outputs;
};

struct OcrTokenizerManifest {
    QString type;
    QString vocabFile;
    QString sha256;
    int bosTokenId{1};
    int eosTokenId{2};
    int padTokenId{0};
    int unkTokenId{3};
};

struct OcrInputManifest {
    int width{224};
    int height{224};
    QString layout;
    QString color;
    QString sourceConversion;
    QString resizeMode;
    QString resample;
};

struct OcrNormalizationManifest {
    float scale{1.0f / 255.0f};
    std::array<float, 3> mean{};
    std::array<float, 3> std{};
};

struct OcrGenerationManifest {
    int maxLength{128};
    QString strategy;
    float repetitionPenalty{1.2f};
    int contentRunCap{12};
};

struct OcrModelManifest {
    int manifestVersion{1};
    QString id;
    QString version;
    QString modelDir; // The absolute path where this manifest was loaded from
    OcrInputManifest input;
    OcrNormalizationManifest normalization;
    OcrGenerationManifest generation;
    QVector<OcrGraphManifest> graphs;
    OcrTokenizerManifest tokenizer;
    QString postprocess;
};

[[nodiscard]] application::Result<OcrModelManifest> loadAndValidateManifest(const QString& modelDir);

} // namespace manga::services
