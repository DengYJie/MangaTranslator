#pragma once

#include <compare>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace manga::domain {

enum class LanguageCodeError {
    Empty,
    InvalidCharacter,
    InvalidStructure
};

/// Value type representing a normalized language code such as "ja", "en", or "zh-Hans".
class LanguageCode {
public:
    constexpr LanguageCode() = default;

    /// Parses and normalizes a language code.
    [[nodiscard]] static std::expected<LanguageCode, LanguageCodeError> fromString(std::string_view code);

    /// Constructs without validation for well-known static constants.
    [[nodiscard]] static constexpr LanguageCode createUnchecked(std::string_view code) {
        LanguageCode lang;
        lang.m_code = code;
        return lang;
    }

    [[nodiscard]] const std::string& toString() const noexcept {
        return m_code;
    }
    [[nodiscard]] bool empty() const noexcept {
        return m_code.empty();
    }

    [[nodiscard]] auto operator<=>(const LanguageCode& other) const noexcept = default;
    [[nodiscard]] bool operator==(const LanguageCode& other) const noexcept = default;

private:
    std::string m_code;
};

namespace languages {
inline const LanguageCode Japanese = LanguageCode::createUnchecked("ja");
inline const LanguageCode SimplifiedChinese = LanguageCode::createUnchecked("zh-Hans");
inline const LanguageCode TraditionalChinese = LanguageCode::createUnchecked("zh-Hant");
inline const LanguageCode English = LanguageCode::createUnchecked("en");
inline const LanguageCode Korean = LanguageCode::createUnchecked("ko");
} // namespace languages

} // namespace manga::domain

namespace std {
template <> struct hash<manga::domain::LanguageCode> {
    size_t operator()(const manga::domain::LanguageCode& code) const noexcept {
        return std::hash<std::string>{}(code.toString());
    }
};
} // namespace std
