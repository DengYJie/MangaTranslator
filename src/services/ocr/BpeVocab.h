#pragma once

#include "application/error/ApplicationError.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace manga::services {

/// BPE and Hugging Face tokenizer vocabulary parser.
/// Supports tokenizer.json (Hugging Face / ERNIE / PaddleOCR-VL) and raw JSON arrays.
class BpeVocab final {
public:
    static constexpr int kDefaultBosId = 1;
    static constexpr int kDefaultEosId = 2;
    static constexpr int kDefaultPadId = 0;
    static constexpr int kDefaultUnkId = 3;

    /// Loads a vocab JSON file (either a tokenizer.json object or a JSON array of strings).
    [[nodiscard]] static application::Result<BpeVocab>
    load(const QString& vocabPath, int bosId = -1, int eosId = -1, int padId = -1, int unkId = -1);

    /// Decodes token IDs into a human-readable text string.
    [[nodiscard]] QString decode(const QVector<int>& ids) const;

    /// Lookup token ID by string token. Returns -1 if not found.
    [[nodiscard]] int tokenToId(const QString& token) const;

    /// Lookup string token by ID. Returns empty string if not found.
    [[nodiscard]] QString idToToken(int id) const;

    [[nodiscard]] int bosId() const noexcept {
        return m_bosId;
    }
    [[nodiscard]] int eosId() const noexcept {
        return m_eosId;
    }
    [[nodiscard]] int padId() const noexcept {
        return m_padId;
    }
    [[nodiscard]] int unkId() const noexcept {
        return m_unkId;
    }

    [[nodiscard]] int vocabSize() const noexcept {
        return m_vocabSize;
    }
    [[nodiscard]] bool isSpecialToken(int id) const;

private:
    QHash<int, QString> m_idToToken;
    QHash<QString, int> m_tokenToId;
    QSet<int> m_specialIds;
    int m_vocabSize{0};
    int m_bosId{kDefaultBosId};
    int m_eosId{kDefaultEosId};
    int m_padId{kDefaultPadId};
    int m_unkId{kDefaultUnkId};
};

} // namespace manga::services
