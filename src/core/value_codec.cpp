#include "core/value_codec.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace gamex {
namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

void store_bytes(std::uint64_t value, std::size_t width, Endian endian,
                 std::vector<std::byte> &out) {
  out.resize(width);
  for (std::size_t i = 0; i < width; ++i) {
    const std::size_t shift = endian == Endian::Little ? i : (width - 1 - i);
    out[i] = static_cast<std::byte>((value >> (shift * 8)) & 0xffu);
  }
}

std::uint64_t load_bytes(std::span<const std::byte> bytes, Endian endian) {
  std::uint64_t value = 0;
  const std::size_t width = bytes.size();
  for (std::size_t i = 0; i < width; ++i) {
    const std::size_t shift = endian == Endian::Little ? i : (width - 1 - i);
    value |=
        static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[i]))
        << (shift * 8);
  }
  return value;
}

std::optional<std::uint64_t> parse_u64_base(std::string_view text, int base,
                                            std::string *error) {
  if (text.empty()) {
    if (error) {
      *error = "Порожнє число";
    }
    return std::nullopt;
  }
  std::uint64_t value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    if (error) {
      *error = "Некоректне число";
    }
    return std::nullopt;
  }
  return value;
}

std::optional<std::int64_t> parse_integer(std::string_view text,
                                          std::string *error) {
  text = trim(text);
  if (text.empty()) {
    if (error) {
      *error = "Порожнє число";
    }
    return std::nullopt;
  }
  bool negative = false;
  if (text.front() == '-') {
    negative = true;
    text.remove_prefix(1);
  } else if (text.front() == '+') {
    text.remove_prefix(1);
  }

  int base = 10;
  if (text.size() >= 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  } else if (text.size() >= 2 && (text.back() == 'h' || text.back() == 'H')) {
    base = 16;
    text.remove_suffix(1);
  }

  const auto parsed = parse_u64_base(text, base, error);
  if (!parsed) {
    return std::nullopt;
  }
  if (negative) {
    if (*parsed >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
            1ull) {
      if (error) {
        *error = "Число поза діапазоном";
      }
      return std::nullopt;
    }
    if (*parsed ==
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
            1ull) {
      return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(*parsed);
  }
  if (*parsed >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    if (error) {
      *error = "Число поза діапазоном знакового типу; для великих значень "
               "оберіть беззнаковий тип";
    }
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*parsed);
}

std::optional<double> parse_real(std::string_view text, std::string *error) {
  text = trim(text);
  std::string normalized(text);
  if (normalized.find(',') != std::string::npos &&
      normalized.find('.') == std::string::npos) {
    std::replace(normalized.begin(), normalized.end(), ',', '.');
  }
  double value = 0;
  const auto result = std::from_chars(
      normalized.data(), normalized.data() + normalized.size(), value);
  if (result.ec != std::errc{} ||
      result.ptr != normalized.data() + normalized.size()) {
    if (error) {
      *error = "Некоректне дробове число";
    }
    return std::nullopt;
  }
  return value;
}

std::optional<EncodedValue> encode_hex_bytes(std::string_view text,
                                             std::string *error) {
  std::string hex;
  hex.reserve(text.size());
  for (char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    hex.push_back(ch);
  }
  if (hex.size() < 2 || hex.size() % 2 != 0) {
    if (error) {
      *error = "Байти задаються парами шістнадцяткових цифр, наприклад DE AD";
    }
    return std::nullopt;
  }
  EncodedValue encoded;
  encoded.bytes.resize(hex.size() / 2);
  for (std::size_t i = 0; i < encoded.bytes.size(); ++i) {
    const auto byte =
        parse_u64_base(std::string_view(hex).substr(i * 2, 2), 16, error);
    if (!byte || *byte > 0xff) {
      if (error) {
        *error = "Некоректна шістнадцяткова послідовність";
      }
      return std::nullopt;
    }
    encoded.bytes[i] = static_cast<std::byte>(*byte);
  }
  return encoded;
}

bool fits_unsigned(std::uint64_t value, std::size_t width) {
  if (width >= 8) {
    return true;
  }
  const std::uint64_t max = (1ull << (width * 8)) - 1ull;
  return value <= max;
}

bool fits_signed(std::int64_t value, std::size_t width) {
  if (width >= 8) {
    return true;
  }
  if (width == 3) {
    return value >= -0x800000 && value <= 0x7fffff;
  }
  const int bits = static_cast<int>(width * 8);
  const std::int64_t min = -(1ll << (bits - 1));
  const std::int64_t max = (1ll << (bits - 1)) - 1;
  return value >= min && value <= max;
}

} // namespace

bool is_fixed_width(ValueType type) {
  return type != ValueType::StringUtf8 && type != ValueType::ByteArray;
}

bool is_float_type(ValueType type) {
  return type == ValueType::Float32 || type == ValueType::Float64;
}

bool is_unsigned_type(ValueType type) {
  return type == ValueType::UInt8 || type == ValueType::UInt16 ||
         type == ValueType::UInt32 || type == ValueType::UInt64;
}

std::size_t fixed_width(ValueType type) {
  switch (type) {
  case ValueType::Int8:
  case ValueType::UInt8:
    return 1;
  case ValueType::Int16:
  case ValueType::UInt16:
    return 2;
  case ValueType::Int24:
    return 3;
  case ValueType::Int32:
  case ValueType::UInt32:
  case ValueType::Float32:
    return 4;
  case ValueType::Int64:
  case ValueType::UInt64:
  case ValueType::Float64:
    return 8;
  case ValueType::StringUtf8:
  case ValueType::ByteArray:
    return 0;
  }
  return 0;
}

std::string_view value_type_name(ValueType type) {
  switch (type) {
  case ValueType::Int8:
    return "i8";
  case ValueType::UInt8:
    return "u8";
  case ValueType::Int16:
    return "i16";
  case ValueType::UInt16:
    return "u16";
  case ValueType::Int24:
    return "i24";
  case ValueType::Int32:
    return "i32";
  case ValueType::UInt32:
    return "u32";
  case ValueType::Int64:
    return "i64";
  case ValueType::UInt64:
    return "u64";
  case ValueType::Float32:
    return "f32";
  case ValueType::Float64:
    return "f64";
  case ValueType::StringUtf8:
    return "utf8";
  case ValueType::ByteArray:
    return "bytes";
  }
  return "i32";
}

std::optional<ValueType> value_type_from_name(std::string_view name) {
  constexpr ValueType kTypes[] = {
      ValueType::Int8,     ValueType::UInt8,   ValueType::Int16,
      ValueType::UInt16,   ValueType::Int24,   ValueType::Int32,
      ValueType::UInt32,   ValueType::Int64,   ValueType::UInt64,
      ValueType::Float32,  ValueType::Float64, ValueType::StringUtf8,
      ValueType::ByteArray};
  for (ValueType type : kTypes) {
    if (value_type_name(type) == name) {
      return type;
    }
  }
  return std::nullopt;
}

std::string_view compare_op_name(CompareOp op) {
  switch (op) {
  case CompareOp::Equal:
    return "equal";
  case CompareOp::NotEqual:
    return "not-equal";
  case CompareOp::Greater:
    return "greater";
  case CompareOp::GreaterOrEqual:
    return "greater-or-equal";
  case CompareOp::Less:
    return "less";
  case CompareOp::LessOrEqual:
    return "less-or-equal";
  case CompareOp::Between:
    return "between";
  case CompareOp::Increased:
    return "increased";
  case CompareOp::Decreased:
    return "decreased";
  case CompareOp::IncreasedBy:
    return "increased-by";
  case CompareOp::DecreasedBy:
    return "decreased-by";
  case CompareOp::Changed:
    return "changed";
  case CompareOp::Unchanged:
    return "unchanged";
  case CompareOp::Unknown:
    return "unknown";
  }
  return "equal";
}

std::string_view freeze_mode_name(FreezeMode mode) {
  switch (mode) {
  case FreezeMode::Normal:
    return "normal";
  case FreezeMode::AllowIncrease:
    return "allow-increase";
  case FreezeMode::AllowDecrease:
    return "allow-decrease";
  case FreezeMode::Clamp:
    return "clamp";
  }
  return "normal";
}

std::optional<FreezeMode> freeze_mode_from_name(std::string_view name) {
  constexpr FreezeMode kModes[] = {
      FreezeMode::Normal, FreezeMode::AllowIncrease, FreezeMode::AllowDecrease,
      FreezeMode::Clamp};
  for (FreezeMode mode : kModes) {
    if (freeze_mode_name(mode) == name) {
      return mode;
    }
  }
  return std::nullopt;
}

std::string_view endian_name(Endian endian) {
  return endian == Endian::Big ? "be" : "le";
}

std::optional<Endian> endian_from_name(std::string_view name) {
  if (name == "le") {
    return Endian::Little;
  }
  if (name == "be") {
    return Endian::Big;
  }
  return std::nullopt;
}

std::optional<EncodedValue> encode_i64(ValueType type, Endian endian,
                                       std::int64_t value, std::string *error) {
  if (!is_fixed_width(type) || is_float_type(type)) {
    if (error) {
      *error = "Тип не є цілим фіксованої ширини";
    }
    return std::nullopt;
  }
  const std::size_t width = fixed_width(type);
  if (is_unsigned_type(type)) {
    if (value < 0) {
      if (error) {
        *error = "Від'ємне число для беззнакового типу";
      }
      return std::nullopt;
    }
    const auto uvalue = static_cast<std::uint64_t>(value);
    if (!fits_unsigned(uvalue, width)) {
      if (error) {
        *error = "Число поза діапазоном";
      }
      return std::nullopt;
    }
    EncodedValue encoded;
    store_bytes(uvalue, width, endian, encoded.bytes);
    return encoded;
  }
  if (!fits_signed(value, width)) {
    if (error) {
      *error = "Число поза діапазоном";
    }
    return std::nullopt;
  }
  EncodedValue encoded;
  store_bytes(static_cast<std::uint64_t>(value), width, endian, encoded.bytes);
  return encoded;
}

std::optional<EncodedValue> encode_f64(ValueType type, Endian endian,
                                       double value, std::string *error) {
  if (!is_float_type(type)) {
    if (error) {
      *error = "Тип не є дробовим";
    }
    return std::nullopt;
  }
  EncodedValue encoded;
  if (type == ValueType::Float32) {
    const float narrow = static_cast<float>(value);
    static_assert(sizeof(float) == 4);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &narrow, sizeof(bits));
    store_bytes(bits, 4, endian, encoded.bytes);
    return encoded;
  }
  static_assert(sizeof(double) == 8);
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  store_bytes(bits, 8, endian, encoded.bytes);
  return encoded;
}

std::optional<EncodedValue> encode_text(ValueType type, Endian endian,
                                        std::string_view text,
                                        std::string *error) {
  text = trim(text);
  if (type == ValueType::StringUtf8) {
    if (text.empty()) {
      if (error) {
        *error = "Порожній рядок";
      }
      return std::nullopt;
    }
    EncodedValue encoded;
    encoded.bytes.resize(text.size());
    std::memcpy(encoded.bytes.data(), text.data(), text.size());
    return encoded;
  }
  if (type == ValueType::ByteArray) {
    return encode_hex_bytes(text, error);
  }
  if (is_float_type(type)) {
    const auto real = parse_real(text, error);
    if (!real) {
      return std::nullopt;
    }
    return encode_f64(type, endian, *real, error);
  }
  if (is_unsigned_type(type)) {
    bool negative = false;
    if (!text.empty() && text.front() == '-') {
      negative = true;
      text.remove_prefix(1);
    }
    if (negative) {
      if (error) {
        *error = "Від'ємне число для беззнакового типу";
      }
      return std::nullopt;
    }
    int base = 10;
    if (text.size() >= 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
      base = 16;
      text.remove_prefix(2);
    } else if (text.size() >= 2 && (text.back() == 'h' || text.back() == 'H')) {
      base = 16;
      text.remove_suffix(1);
    }
    const auto parsed = parse_u64_base(text, base, error);
    if (!parsed) {
      return std::nullopt;
    }
    if (!fits_unsigned(*parsed, fixed_width(type))) {
      if (error) {
        *error = "Число поза діапазоном";
      }
      return std::nullopt;
    }
    EncodedValue encoded;
    store_bytes(*parsed, fixed_width(type), endian, encoded.bytes);
    return encoded;
  }
  const auto integer = parse_integer(text, error);
  if (!integer) {
    return std::nullopt;
  }
  return encode_i64(type, endian, *integer, error);
}

std::optional<Number> decode_number(ValueType type, Endian endian,
                                    std::span<const std::byte> bytes) {
  if (!is_fixed_width(type) || bytes.size() != fixed_width(type)) {
    return std::nullopt;
  }
  Number number;
  if (type == ValueType::Float32) {
    const auto bits = static_cast<std::uint32_t>(load_bytes(bytes, endian));
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    number.is_float = true;
    number.real = static_cast<double>(value);
    return number;
  }
  if (type == ValueType::Float64) {
    const auto bits = load_bytes(bytes, endian);
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    number.is_float = true;
    number.real = value;
    return number;
  }
  const std::uint64_t raw = load_bytes(bytes, endian);
  number.uinteger = raw;
  number.is_signed = !is_unsigned_type(type);
  if (!number.is_signed) {
    number.integer = raw > static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())
                         ? std::numeric_limits<std::int64_t>::max()
                         : static_cast<std::int64_t>(raw);
    number.real = static_cast<double>(raw);
    return number;
  }
  const std::size_t width = bytes.size();
  if (width < 8) {
    const int bits = static_cast<int>(width * 8);
    const std::uint64_t sign = 1ull << (bits - 1);
    if ((raw & sign) != 0) {
      const std::uint64_t extended = raw | (~0ull << bits);
      number.integer = static_cast<std::int64_t>(extended);
    } else {
      number.integer = static_cast<std::int64_t>(raw);
    }
  } else {
    number.integer = static_cast<std::int64_t>(raw);
  }
  number.real = static_cast<double>(number.integer);
  return number;
}

std::string decode_text(ValueType type, Endian endian,
                        std::span<const std::byte> bytes) {
  if (type == ValueType::StringUtf8) {
    std::string text;
    text.resize(bytes.size());
    std::memcpy(text.data(), bytes.data(), bytes.size());
    return text;
  }
  if (type == ValueType::ByteArray) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (i != 0) {
        text.push_back(' ');
      }
      const auto value = std::to_integer<unsigned char>(bytes[i]);
      text.push_back(kHex[value >> 4]);
      text.push_back(kHex[value & 0x0f]);
    }
    return text;
  }
  const auto number = decode_number(type, endian, bytes);
  if (!number) {
    return {};
  }
  if (number->is_float) {
    std::string text(64, '\0');
    const auto result =
        std::to_chars(text.data(), text.data() + text.size(), number->real);
    if (result.ec != std::errc{}) {
      return {};
    }
    text.resize(static_cast<std::size_t>(result.ptr - text.data()));
    return text;
  }
  if (!number->is_signed) {
    std::string text(32, '\0');
    const auto result =
        std::to_chars(text.data(), text.data() + text.size(), number->uinteger);
    if (result.ec != std::errc{}) {
      return {};
    }
    text.resize(static_cast<std::size_t>(result.ptr - text.data()));
    return text;
  }
  std::string text(32, '\0');
  const auto result =
      std::to_chars(text.data(), text.data() + text.size(), number->integer);
  if (result.ec != std::errc{}) {
    return {};
  }
  text.resize(static_cast<std::size_t>(result.ptr - text.data()));
  return text;
}

int compare_numbers(const Number &left, const Number &right) {
  if (left.is_float || right.is_float) {
    if (left.real < right.real) {
      return -1;
    }
    if (left.real > right.real) {
      return 1;
    }
    return 0;
  }
  if (!left.is_signed && !right.is_signed) {
    if (left.uinteger < right.uinteger) {
      return -1;
    }
    if (left.uinteger > right.uinteger) {
      return 1;
    }
    return 0;
  }
  if (left.integer < right.integer) {
    return -1;
  }
  if (left.integer > right.integer) {
    return 1;
  }
  return 0;
}

} // namespace gamex
