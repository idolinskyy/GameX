#include "core/search_value.hpp"

#include "core/expression.hpp"

namespace gamex {

std::optional<PreparedValue> prepare_search_value(ValueType type, Endian endian, std::string_view text,
                                                  std::string* error) {
    if (type == ValueType::ByteArray) {
        auto pattern = parse_byte_pattern(text, error);
        if (!pattern) {
            return std::nullopt;
        }
        PreparedValue prepared;
        prepared.value.bytes = std::move(pattern->bytes);
        prepared.mask = std::move(pattern->mask);
        return prepared;
    }
    if (type == ValueType::StringUtf8) {
        auto encoded = encode_text(type, endian, text, error);
        if (!encoded) {
            return std::nullopt;
        }
        return PreparedValue{std::move(*encoded), {}};
    }
    std::string body(text);
    if (looks_like_expression(text)) {
        if (is_float_type(type)) {
            const auto value = eval_f64(text, {}, error);
            if (!value) {
                return std::nullopt;
            }
            auto encoded = encode_f64(type, endian, *value, error);
            if (!encoded) {
                return std::nullopt;
            }
            return PreparedValue{std::move(*encoded), {}};
        }
        const auto value = eval_i64(text, {}, error);
        if (!value) {
            return std::nullopt;
        }
        if (is_unsigned_type(type) && *value < 0) {
            if (error) {
                *error = "Від'ємний результат для беззнакового типу";
            }
            return std::nullopt;
        }
        auto encoded = encode_i64(type, endian, *value, error);
        if (!encoded) {
            return std::nullopt;
        }
        return PreparedValue{std::move(*encoded), {}};
    }
    auto encoded = encode_text(type, endian, text, error);
    if (!encoded) {
        return std::nullopt;
    }
    return PreparedValue{std::move(*encoded), {}};
}

}  // namespace gamex
