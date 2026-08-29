#include "BaberuVocab.h"

#include <QChar>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace manga::services {

application::Result<BaberuVocab>
BaberuVocab::load(const QString& vocabPath, int bosId, int eosId, int padId, int unkId) {
    QFile f(vocabPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.incompatible"),
                                                                 QStringLiteral("Cannot read vocab file"),
                                                                 application::RecoveryAction::LocateModel));
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.incompatible"),
                                                                 QStringLiteral("Vocab file is not a valid JSON array"),
                                                                 application::RecoveryAction::LocateModel));
    }

    BaberuVocab vocab;
    vocab.m_bosId = bosId;
    vocab.m_eosId = eosId;
    vocab.m_padId = padId;
    vocab.m_unkId = unkId;

    QJsonArray arr = doc.array();
    vocab.m_chars.reserve(arr.size());

    // Characters to exclude from content_ids
    const QSet<QChar> waveDashChars = {
        QChar(0x30FC), // ー Katakana-Hiragana Prolonged Sound Mark
        QChar(0xFF70), // ｰ Halfwidth Katakana-Hiragana Prolonged Sound Mark
        QChar(0x301C), // 〜 Wave Dash
        QChar(0x007E)  // ~ Tilde
    };

    int currentId = kOffset;
    for (const QJsonValue& val : arr) {
        if (!val.isString()) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Model,
                                                  QStringLiteral("model.incompatible"),
                                                  QStringLiteral("Vocab array contains non-string element"),
                                                  application::RecoveryAction::LocateModel));
        }
        QString s = val.toString();
        vocab.m_chars.append(s);

        if (s.length() == 1) {
            QChar ch = s.at(0);
            QChar::Category cat = ch.category();
            // L = Letter, N = Number
            if ((cat >= QChar::Letter_Uppercase && cat <= QChar::Letter_Other) ||
                (cat >= QChar::Number_DecimalDigit && cat <= QChar::Number_Other)) {
                if (!waveDashChars.contains(ch)) {
                    vocab.m_contentIds.insert(currentId);
                }
            }
        }
        currentId++;
    }

    return vocab;
}

QString BaberuVocab::decode(const QVector<int>& ids) const {
    QString result;
    result.reserve(ids.size()); // Appending one char per token on average
    for (int id : ids) {
        if (id == m_bosId || id == m_eosId || id == m_padId || id == m_unkId) {
            continue;
        }
        if (id >= kOffset && (id - kOffset) < m_chars.size()) {
            result.append(m_chars.at(id - kOffset));
        }
    }
    return result;
}

} // namespace manga::services
