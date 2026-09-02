#include "domain/model/LanguageCode.h"

#include <cctype>

namespace manga::domain {

std::expected<LanguageCode, LanguageCodeError> LanguageCode::fromString(std::string_view code) {
    if (code.empty()) {
        return std::unexpected(LanguageCodeError::Empty);
    }
    if (code.size() > 32) {
        return std::unexpected(LanguageCodeError::InvalidStructure);
    }

    std::string normalized;
    normalized.reserve(code.size());

    bool prevHyphen = true; // Seeding the flag rejects a leading hyphen or underscore.
    for (size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '-' || c == '_') {
            if (prevHyphen) {
                return std::unexpected(LanguageCodeError::InvalidStructure);
            }
            normalized.push_back('-');
            prevHyphen = true;
        } else if (std::isalnum(static_cast<unsigned char>(c))) {
            normalized.push_back(c);
            prevHyphen = false;
        } else {
            return std::unexpected(LanguageCodeError::InvalidCharacter);
        }
    }

    if (prevHyphen) { // A set flag after the loop means the code ends with a hyphen or underscore.
        return std::unexpected(LanguageCodeError::InvalidStructure);
    }

    LanguageCode result;
    result.m_code = std::move(normalized);
    return result;
}

} // namespace manga::domain
