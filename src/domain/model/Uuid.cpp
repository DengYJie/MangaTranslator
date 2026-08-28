#include "domain/model/Uuid.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace manga::domain {

namespace {

constexpr int parseHexNibble(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

constexpr char toHexLower(uint8_t nibble) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    return digits[nibble & 0x0F];
}

} // namespace

Uuid Uuid::generateV4() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    const uint64_t part1 = dist(generator);
    const uint64_t part2 = dist(generator);

    std::array<uint8_t, kByteCount> bytes{};
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((part1 >> (i * 8)) & 0xFF);
        bytes[i + 8] = static_cast<uint8_t>((part2 >> (i * 8)) & 0xFF);
    }

    // Set RFC 4122 v4 version (0100 in bits 12-15 of time_hi_and_version, i.e., byte 6)
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    // Set RFC 4122 variant (10 in bits 6-7 of clock_seq_hi_and_reserved, i.e., byte 8)
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

    return Uuid{bytes};
}

std::expected<Uuid, UuidParseError> Uuid::parse(std::string_view text) {
    if (text.size() != kStringLength) {
        return std::unexpected(UuidParseError::InvalidLength);
    }

    if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return std::unexpected(UuidParseError::InvalidHyphenPlacement);
    }

    std::array<uint8_t, kByteCount> bytes{};
    size_t byteIdx = 0;

    for (size_t i = 0; i < text.size();) {
        if (text[i] == '-') {
            ++i;
            continue;
        }

        const int hi = parseHexNibble(text[i]);
        const int lo = parseHexNibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::unexpected(UuidParseError::InvalidHexCharacter);
        }

        bytes[byteIdx++] = static_cast<uint8_t>((hi << 4) | lo);
        i += 2;
    }

    // Validate RFC 4122 v4 version: byte 6 high nibble must be 4
    if ((bytes[6] >> 4) != 4) {
        return std::unexpected(UuidParseError::InvalidVersion);
    }

    // Validate RFC 4122 variant: byte 8 high 2 bits must be 0b10 (0x80..0xBF)
    if ((bytes[8] & 0xC0) != 0x80) {
        return std::unexpected(UuidParseError::InvalidVariant);
    }

    return Uuid{bytes};
}

std::string Uuid::toString() const {
    std::string result;
    result.reserve(kStringLength);

    for (size_t i = 0; i < kByteCount; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            result.push_back('-');
        }
        result.push_back(toHexLower(static_cast<uint8_t>(m_bytes[i] >> 4)));
        result.push_back(toHexLower(static_cast<uint8_t>(m_bytes[i] & 0x0F)));
    }

    return result;
}

} // namespace manga::domain
