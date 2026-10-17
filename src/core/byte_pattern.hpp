#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gamex {

struct BytePattern {
    std::vector<std::byte> bytes;
    std::vector<std::byte> mask;  // 0xff = враховувати, 0x00 = будь-який

    bool empty() const { return bytes.empty(); }
    std::size_t size() const { return bytes.size(); }
    bool has_wildcards() const;
    bool matches(std::span<const std::byte> data) const;
};

std::optional<BytePattern> parse_byte_pattern(std::string_view text, std::string* error);

}  // namespace gamex
