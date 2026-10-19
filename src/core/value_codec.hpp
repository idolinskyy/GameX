#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gamex {

enum class ValueType {
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int24,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Float32,
  Float64,
  StringUtf8,
  ByteArray,
};

enum class Endian {
  Little,
  Big,
};

enum class CompareOp {
  Equal,
  NotEqual,
  Greater,
  GreaterOrEqual,
  Less,
  LessOrEqual,
  Between,
  Increased,
  Decreased,
  IncreasedBy, // зросло рівно на value (відсів)
  DecreasedBy, // зменшилось рівно на value (відсів)
  Changed,
  Unchanged,
  Unknown,
};

enum class FreezeMode {
  Normal,
  AllowIncrease,
  AllowDecrease,
  Clamp,
};

struct EncodedValue {
  std::vector<std::byte> bytes;
};

struct Number {
  bool is_float = false;
  bool is_signed = true;
  std::int64_t integer = 0;
  std::uint64_t uinteger = 0;
  double real = 0;
};

bool is_fixed_width(ValueType type);
bool is_float_type(ValueType type);
bool is_unsigned_type(ValueType type);
std::size_t fixed_width(ValueType type);
std::string_view value_type_name(ValueType type);
std::optional<ValueType> value_type_from_name(std::string_view name);
std::string_view compare_op_name(CompareOp op);
std::string_view freeze_mode_name(FreezeMode mode);
std::optional<FreezeMode> freeze_mode_from_name(std::string_view name);
std::string_view endian_name(Endian endian);
std::optional<Endian> endian_from_name(std::string_view name);

std::optional<EncodedValue> encode_text(ValueType type, Endian endian,
                                        std::string_view text,
                                        std::string *error);
std::optional<EncodedValue> encode_i64(ValueType type, Endian endian,
                                       std::int64_t value, std::string *error);
std::optional<EncodedValue> encode_f64(ValueType type, Endian endian,
                                       double value, std::string *error);

std::string decode_text(ValueType type, Endian endian,
                        std::span<const std::byte> bytes);
std::optional<Number> decode_number(ValueType type, Endian endian,
                                    std::span<const std::byte> bytes);

int compare_numbers(const Number &left, const Number &right);

} // namespace gamex
