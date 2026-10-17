#include "core/byte_pattern.hpp"

#include <cctype>

namespace gamex {

bool BytePattern::has_wildcards() const {
    for (std::byte bit : mask) {
        if (bit != std::byte{0xff}) {
            return true;
        }
    }
    return false;
}

bool BytePattern::matches(std::span<const std::byte> data) const {
    if (data.size() < bytes.size() || bytes.size() != mask.size()) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (mask[i] == std::byte{0x00}) {
            continue;
        }
        if (data[i] != bytes[i]) {
            return false;
        }
    }
    return true;
}

std::optional<BytePattern> parse_byte_pattern(std::string_view text, std::string* error) {
    BytePattern pattern;
    std::size_t i = 0;
    auto skip_space = [&] {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    };
    skip_space();
    if (i >= text.size()) {
        if (error) {
            *error = "Порожній шаблон байтів";
        }
        return std::nullopt;
    }
    while (i < text.size()) {
        skip_space();
        if (i >= text.size()) {
            break;
        }
        if (i + 1 < text.size() && text[i] == '?' && text[i + 1] == '?') {
            pattern.bytes.push_back(std::byte{0});
            pattern.mask.push_back(std::byte{0x00});
            i += 2;
            continue;
        }
        if (text[i] == '?') {
            pattern.bytes.push_back(std::byte{0});
            pattern.mask.push_back(std::byte{0x00});
            ++i;
            continue;
        }
        if (i + 1 >= text.size() || !std::isxdigit(static_cast<unsigned char>(text[i])) ||
            !std::isxdigit(static_cast<unsigned char>(text[i + 1]))) {
            if (error) {
                *error = "Байти: пари hex або ??, наприклад DE ?? AD";
            }
            return std::nullopt;
        }
        auto hex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return ch - 'a' + 10;
            }
            if (ch >= 'A' && ch <= 'F') {
                return ch - 'A' + 10;
            }
            return -1;
        };
        const int hi = hex(text[i]);
        const int lo = hex(text[i + 1]);
        pattern.bytes.push_back(static_cast<std::byte>((hi << 4) | lo));
        pattern.mask.push_back(std::byte{0xff});
        i += 2;
    }
    if (pattern.bytes.empty()) {
        if (error) {
            *error = "Порожній шаблон байтів";
        }
        return std::nullopt;
    }
    return pattern;
}

}  // namespace gamex
