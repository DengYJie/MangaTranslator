#pragma once

#include "application/error/ApplicationError.h"

#include <QSet>
#include <QString>
#include <QVector>

namespace manga::services {

/// Character-level vocabulary for Baberu OCR.
/// Token ids 0 to 3 are special: 0 pad, 1 bos, 2 eos, 3 unk.
/// Ids from 4 up map to the charset at id minus 4.
class BaberuVocab final {
public:
    static constexpr int kPadId = 0;
    static constexpr int kBosId = 1;
    static constexpr int kEosId = 2;
    static constexpr int kUnkId = 3;
    static constexpr int kOffset = 4;

    /// Loads a vocab JSON file holding an array of single characters.
    [[nodiscard]] static application::Result<BaberuVocab>
    load(const QString& vocabPath, int bosId, int eosId, int padId, int unkId);

    /// Decodes token ids into a string, skipping the special ids and any id outside the loaded charset.
    [[nodiscard]] QString decode(const QVector<int>& ids) const;

    /// Content token ids, the single-character letters and digits, excluding the prolonged sound mark,
    /// the wave dash, and the tilde.
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
