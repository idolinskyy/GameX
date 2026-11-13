#include "core/search_value.hpp"

#include "core/expression.hpp"

#include <cctype>
#include <vector>

namespace gamex {
namespace {

std::vector<std::string> split_sequence_tokens(std::string_view text) {
  std::vector<std::string> tokens;
  std::string current;
  auto flush = [&] {
    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  };
  for (char ch : text) {
    const unsigned char u = static_cast<unsigned char>(ch);
    if (std::isspace(u) || ch == ',' || ch == ';') {
      flush();
      continue;
    }
    current.push_back(ch);
  }
  flush();
  return tokens;
}

} // namespace

std::optional<PreparedValue> prepare_search_value(ValueType type, Endian endian,
                                                  std::string_view text,
                                                  std::string *error) {
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

std::optional<StructureTemplate>
make_sequence_structure(std::string_view name, ValueType type, Endian endian,
                        std::string_view text, std::string *error) {
  if (!is_fixed_width(type)) {
    if (error) {
      *error = "Послідовність працює лише з фіксованою шириною (ціле / дріб), "
               "не з рядком чи «Байти»";
    }
    return std::nullopt;
  }
  const auto tokens = split_sequence_tokens(text);
  if (tokens.size() < 2) {
    if (error) {
      *error = "Введіть щонайменше два значення через пробіл, наприклад: "
               "1000 555 333 5555";
    }
    return std::nullopt;
  }
  constexpr std::size_t kMaxFields = 64;
  if (tokens.size() > kMaxFields) {
    if (error) {
      *error = "Забагато значень у послідовності (макс. 64)";
    }
    return std::nullopt;
  }

  const std::size_t width = fixed_width(type);
  StructureTemplate tmpl;
  tmpl.name = name.empty() ? std::string("Sequence") : std::string(name);
  tmpl.endian = endian;
  tmpl.fields.reserve(tokens.size());
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    std::string field_error;
    auto prepared = prepare_search_value(type, endian, tokens[i], &field_error);
    if (!prepared) {
      if (error) {
        *error =
            "Значення #" + std::to_string(i + 1) + " («" + tokens[i] + "»): " +
            (field_error.empty() ? std::string("не розпізнано") : field_error);
      }
      return std::nullopt;
    }
    StructureField field;
    field.offset = i * width;
    field.type = type;
    field.description = "[" + std::to_string(i) + "]";
    field.value = std::move(prepared->value);
    tmpl.fields.push_back(std::move(field));
  }
  if (!tmpl.validate(error)) {
    return std::nullopt;
  }
  return tmpl;
}

} // namespace gamex
