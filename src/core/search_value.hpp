#pragma once

#include "core/byte_pattern.hpp"
#include "core/scanner.hpp"
#include "core/value_codec.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace gamex {

struct PreparedValue {
    EncodedValue value;
    std::vector<std::byte> mask;
};

// Готує значення для пошуку: вираз (100*10, 100 xor 1) або шаблон DE ?? AD.
std::optional<PreparedValue> prepare_search_value(ValueType type, Endian endian, std::string_view text,
                                                  std::string* error);

}  // namespace gamex
