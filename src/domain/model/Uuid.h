#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace manga::domain {

enum class UuidParseError {
    InvalidLength,
    InvalidHyphenPlacement,
    InvalidHexCharacter,
    InvalidVersion,
    InvalidVariant
};

/// RFC 4122 compliant UUID v4 value type.
class Uuid {
public:
    static constexpr size_t kByteCount = 16;
    static constexpr size_t kStringLength = 36;

    /// Constructs a nil (all-zero) UUID.
    constexpr Uuid() noexcept : m_bytes{} {}

    /// Constructs a UUID from 16 raw bytes.
    explicit constexpr Uuid(const std::array<uint8_t, kByteCount>& bytes) noexcept : m_bytes(bytes) {}

    /// Generates a new random RFC 4122 v4 UUID.
    [[nodiscard]] static Uuid generateV4();

    /// Parses a 36-character hyphenated UUID string and validates strictly for RFC 4122 v4.
    [[nodiscard]] static std::expected<Uuid, UuidParseError> parse(std::string_view text);

    /// Checks if this is a nil UUID (all bytes zero).
    [[nodiscard]] constexpr bool isNil() const noexcept {
        for (const auto b : m_bytes) {
            if (b != 0) {
                return false;
            }
        }
        return true;
    }

    /// Checks if this UUID has valid RFC 4122 v4 version and variant bits.
    [[nodiscard]] constexpr bool isValidV4() const noexcept {
        if (isNil()) {
            return false;
        }
        const uint8_t version = static_cast<uint8_t>(m_bytes[6] >> 4);
        const uint8_t variant = static_cast<uint8_t>((m_bytes[8] >> 6) & 0x03);
        return version == 4 && variant == 2; // 0b10 == 2
    }

    /// Returns the raw 16-byte representation.
    [[nodiscard]] constexpr const std::array<uint8_t, kByteCount>& bytes() const noexcept {
        return m_bytes;
    }

    /// Formats as a 36-character lowercase hyphenated string: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx.
    [[nodiscard]] std::string toString() const;

    /// Strong ordering comparison.
    [[nodiscard]] constexpr auto operator<=>(const Uuid& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const Uuid& other) const noexcept = default;

private:
    std::array<uint8_t, kByteCount> m_bytes{};
};

// Strongly-typed ID wrappers to avoid mixing up Project, Page, and Region IDs
struct ProjectId {
    Uuid value;

    [[nodiscard]] static ProjectId generate() {
        return ProjectId{Uuid::generateV4()};
    }
    [[nodiscard]] static std::expected<ProjectId, UuidParseError> parse(std::string_view text) {
        auto res = Uuid::parse(text);
        if (!res) {
            return std::unexpected(res.error());
        }
        return ProjectId{*res};
    }
    [[nodiscard]] bool isValid() const noexcept {
        return value.isValidV4();
    }
    [[nodiscard]] std::string toString() const {
        return value.toString();
    }
    [[nodiscard]] constexpr auto operator<=>(const ProjectId&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const ProjectId&) const noexcept = default;
};

struct PageId {
    Uuid value;

    [[nodiscard]] static PageId generate() {
        return PageId{Uuid::generateV4()};
    }
    [[nodiscard]] static std::expected<PageId, UuidParseError> parse(std::string_view text) {
        auto res = Uuid::parse(text);
        if (!res) {
            return std::unexpected(res.error());
        }
        return PageId{*res};
    }
    [[nodiscard]] bool isValid() const noexcept {
        return value.isValidV4();
    }
    [[nodiscard]] std::string toString() const {
        return value.toString();
    }
    [[nodiscard]] constexpr auto operator<=>(const PageId&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const PageId&) const noexcept = default;
};

struct RegionId {
    Uuid value;

    [[nodiscard]] static RegionId generate() {
        return RegionId{Uuid::generateV4()};
    }
    [[nodiscard]] static std::expected<RegionId, UuidParseError> parse(std::string_view text) {
        auto res = Uuid::parse(text);
        if (!res) {
            return std::unexpected(res.error());
        }
        return RegionId{*res};
    }
    [[nodiscard]] bool isValid() const noexcept {
        return value.isValidV4();
    }
    [[nodiscard]] std::string toString() const {
        return value.toString();
    }
    [[nodiscard]] constexpr auto operator<=>(const RegionId&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const RegionId&) const noexcept = default;
};

struct GlossaryId {
    Uuid value;

    [[nodiscard]] static GlossaryId generate() {
        return GlossaryId{Uuid::generateV4()};
    }
    [[nodiscard]] static std::expected<GlossaryId, UuidParseError> parse(std::string_view text) {
        auto res = Uuid::parse(text);
        if (!res) {
            return std::unexpected(res.error());
        }
        return GlossaryId{*res};
    }
    [[nodiscard]] bool isValid() const noexcept {
        return value.isValidV4();
    }
    [[nodiscard]] std::string toString() const {
        return value.toString();
    }
    [[nodiscard]] constexpr auto operator<=>(const GlossaryId&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const GlossaryId&) const noexcept = default;
};

} // namespace manga::domain

namespace std {
template <> struct hash<manga::domain::Uuid> {
    size_t operator()(const manga::domain::Uuid& uuid) const noexcept {
        const auto& bytes = uuid.bytes();
        size_t h = 0;
        for (const auto b : bytes) {
            h = (h * 131) + b;
        }
        return h;
    }
};

template <> struct hash<manga::domain::ProjectId> {
    size_t operator()(const manga::domain::ProjectId& id) const noexcept {
        return std::hash<manga::domain::Uuid>{}(id.value);
    }
};

template <> struct hash<manga::domain::PageId> {
    size_t operator()(const manga::domain::PageId& id) const noexcept {
        return std::hash<manga::domain::Uuid>{}(id.value);
    }
};

template <> struct hash<manga::domain::RegionId> {
    size_t operator()(const manga::domain::RegionId& id) const noexcept {
        return std::hash<manga::domain::Uuid>{}(id.value);
    }
};

template <> struct hash<manga::domain::GlossaryId> {
    size_t operator()(const manga::domain::GlossaryId& id) const noexcept {
        return std::hash<manga::domain::Uuid>{}(id.value);
    }
};
} // namespace std
