#pragma once

#include "core/byte_pattern.hpp"
#include "core/scanner.hpp"
#include "core/structure.hpp"
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

// Послідовність чисел одного типу підряд (ресурси: «1000 555 333 5555»).
// Зсуви 0, w, 2w, … де w = ширина типу. Лише fixed-width типи.
std::optional<StructureTemplate> make_sequence_structure(std::string_view name, ValueType type,
                                                         Endian endian, std::string_view text,
                                                         std::string* error);

}  // namespace gamex
