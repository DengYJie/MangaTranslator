#pragma once

#include "application/error/ApplicationError.h"

#include <QSet>
#include <QString>
#include <QVector>

namespace manga::services {

// Character-level vocabulary for Baberu OCR.
// Token IDs 0-3 are special: 0=pad, 1=bos, 2=eos, 3=unk.
// IDs >= 4 map to charset[id-4].
class BaberuVocab final {
public:
    static constexpr int kPadId = 0;
    static constexpr int kBosId = 1;
    static constexpr int kEosId = 2;
    static constexpr int kUnkId = 3;
    static constexpr int kOffset = 4;

    // Loads from a vocab JSON file (array of single characters).
    [[nodiscard]] static application::Result<BaberuVocab>
    load(const QString& vocabPath, int bosId, int eosId, int padId, int unkId);

    // Decodes token IDs to a UTF-8 QString; IDs < kOffset are skipped.
    [[nodiscard]] QString decode(const QVector<int>& ids) const;

    // Returns the set of content token IDs: single-char tokens that are letter or
    // digit (Unicode category starts with L or N), excluding wave/dash characters.
    [[nodiscard]] const QSet<int>& contentIds() const noexcept {
        return m_contentIds;
    }

    [[nodiscard]] int vocabSize() const noexcept {
        return m_chars.size() + kOffset;
    }

private:
    QVector<QString> m_chars; // index 0 = id 4
    QSet<int> m_contentIds;
    int m_bosId{kBosId};
    int m_eosId{kEosId};
    int m_padId{kPadId};
    int m_unkId{kUnkId};
};

} // namespace manga::services
