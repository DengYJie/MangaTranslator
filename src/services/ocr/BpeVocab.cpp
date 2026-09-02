#include "BpeVocab.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace manga::services {

application::Result<BpeVocab> BpeVocab::load(const QString& vocabPath, int bosId, int eosId, int padId, int unkId) {
    QFile f(vocabPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.incompatible"),
                                                                 QStringLiteral("Cannot read vocab file: ") + vocabPath,
                                                                 application::RecoveryAction::LocateModel));
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.incompatible"),
                                                                 QStringLiteral("Vocab file JSON parse error: ") +
                                                                     parseError.errorString(),
                                                                 application::RecoveryAction::LocateModel));
    }

    BpeVocab vocab;
    int maxId = 0;

    if (doc.isArray()) {
        // Raw array format (Baberu style)
        QJsonArray arr = doc.array();
        constexpr int offset = 4;
        vocab.m_bosId = (bosId >= 0) ? bosId : kDefaultBosId;
        vocab.m_eosId = (eosId >= 0) ? eosId : kDefaultEosId;
        vocab.m_padId = (padId >= 0) ? padId : kDefaultPadId;
        vocab.m_unkId = (unkId >= 0) ? unkId : kDefaultUnkId;

        vocab.m_specialIds.insert(vocab.m_bosId);
        vocab.m_specialIds.insert(vocab.m_eosId);
        vocab.m_specialIds.insert(vocab.m_padId);
        vocab.m_specialIds.insert(vocab.m_unkId);

        for (int i = 0; i < arr.size(); ++i) {
            int id = i + offset;
            QString s = arr.at(i).toString();
            vocab.m_idToToken.insert(id, s);
            vocab.m_tokenToId.insert(s, id);
            if (id > maxId) {
                maxId = id;
            }
        }
        vocab.m_vocabSize = arr.size() + offset;
    } else if (doc.isObject()) {
        QJsonObject root = doc.object();

        // Parse added_tokens
        if (root.contains(QStringLiteral("added_tokens")) && root.value(QStringLiteral("added_tokens")).isArray()) {
            QJsonArray added = root.value(QStringLiteral("added_tokens")).toArray();
            for (const auto& val : added) {
                if (val.isObject()) {
                    QJsonObject item = val.toObject();
                    int id = item.value(QStringLiteral("id")).toInt();
                    QString content = item.value(QStringLiteral("content")).toString();
                    bool isSpecial = item.value(QStringLiteral("special")).toBool(false);
                    vocab.m_idToToken.insert(id, content);
                    vocab.m_tokenToId.insert(content, id);
                    if (isSpecial || content.startsWith(QStringLiteral("<|")) ||
                        content.startsWith(QStringLiteral("<mask:"))) {
                        vocab.m_specialIds.insert(id);
                    }
                    if (id > maxId) {
                        maxId = id;
                    }
                }
            }
        }

        // Parse model.vocab
        if (root.contains(QStringLiteral("model")) && root.value(QStringLiteral("model")).isObject()) {
            QJsonObject modelObj = root.value(QStringLiteral("model")).toObject();
            if (modelObj.contains(QStringLiteral("vocab")) && modelObj.value(QStringLiteral("vocab")).isObject()) {
                QJsonObject vocabObj = modelObj.value(QStringLiteral("vocab")).toObject();
                for (auto it = vocabObj.begin(); it != vocabObj.end(); ++it) {
                    QString token = it.key();
                    int id = it.value().toInt();
                    vocab.m_idToToken.insert(id, token);
                    vocab.m_tokenToId.insert(token, id);
                    if (token.startsWith(QStringLiteral("<|")) || token.startsWith(QStringLiteral("<mask:"))) {
                        vocab.m_specialIds.insert(id);
                    }
                    if (id > maxId) {
                        maxId = id;
                    }
                }
            }
        }

        // Special tokens defaults for ERNIE / PaddleOCR-VL
        vocab.m_bosId = (bosId >= 0) ? bosId : vocab.m_tokenToId.value(QStringLiteral("<|begin_of_sentence|>"), 100273);
        vocab.m_eosId = (eosId >= 0) ? eosId : vocab.m_tokenToId.value(QStringLiteral("<|end_of_sentence|>"), 100272);
        vocab.m_padId = (padId >= 0) ? padId : vocab.m_tokenToId.value(QStringLiteral("<unk>"), 0);
        vocab.m_unkId = (unkId >= 0) ? unkId : vocab.m_tokenToId.value(QStringLiteral("<unk>"), 0);

        vocab.m_specialIds.insert(vocab.m_bosId);
        vocab.m_specialIds.insert(vocab.m_eosId);
        vocab.m_specialIds.insert(vocab.m_padId);
        vocab.m_specialIds.insert(vocab.m_unkId);
        vocab.m_specialIds.insert(vocab.m_tokenToId.value(QStringLiteral("<s>"), 1));
        vocab.m_specialIds.insert(vocab.m_tokenToId.value(QStringLiteral("</s>"), 2));
        vocab.m_specialIds.insert(vocab.m_tokenToId.value(QStringLiteral("<|IMAGE_START|>"), 101305));
        vocab.m_specialIds.insert(vocab.m_tokenToId.value(QStringLiteral("<|IMAGE_END|>"), 101306));
        vocab.m_specialIds.insert(vocab.m_tokenToId.value(QStringLiteral("<|IMAGE_PLACEHOLDER|>"), 100295));

        vocab.m_vocabSize = maxId + 1;
    } else {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Model,
                                                                 QStringLiteral("model.incompatible"),
                                                                 QStringLiteral("Vocab file format not recognized"),
                                                                 application::RecoveryAction::LocateModel));
    }

    return vocab;
}

QString BpeVocab::decode(const QVector<int>& ids) const {
    QString res;
    res.reserve(ids.size() * 2);

    for (int id : ids) {
        if (m_specialIds.contains(id) || id == m_bosId || id == m_eosId || id == m_padId) {
            continue;
        }

        auto it = m_idToToken.find(id);
        if (it == m_idToToken.end()) {
            continue;
        }

        QString piece = it.value();
        if (piece == QStringLiteral("<nl>")) {
            res.append(QLatin1Char('\n'));
            continue;
        }

        // SentencePiece prefix replacement (U+2581 Lower One Eighth Block)
        piece.replace(QChar(0x2581), QStringLiteral(" "));
        res.append(piece);
    }

    return res.trimmed();
}

int BpeVocab::tokenToId(const QString& token) const {
    return m_tokenToId.value(token, -1);
}

QString BpeVocab::idToToken(int id) const {
    return m_idToToken.value(id, QString());
}

bool BpeVocab::isSpecialToken(int id) const {
    return m_specialIds.contains(id);
}

} // namespace manga::services
