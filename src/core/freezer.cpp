#include "core/freezer.hpp"

#include <charconv>
#include <limits>
#include <vector>

namespace gamex {
namespace {

std::optional<Number> bound_number(const AddressEntry &entry,
                                   std::string_view text) {
  if (text.empty() || text == "-") {
    return std::nullopt;
  }
  auto encoded = encode_text(entry.type, entry.endian, text, nullptr);
  if (!encoded) {
    return std::nullopt;
  }
  return decode_number(entry.type, entry.endian, encoded->bytes);
}

std::vector<std::byte> encode_from_number(const AddressEntry &entry,
                                          const Number &number) {
  if (number.is_float) {
    if (auto encoded =
            encode_f64(entry.type, entry.endian, number.real, nullptr)) {
      return std::move(encoded->bytes);
    }
    return {};
  }
  if (!number.is_signed) {
    if (number.uinteger >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      std::string text(32, '\0');
      const auto result = std::to_chars(text.data(), text.data() + text.size(),
                                        number.uinteger);
      if (result.ec != std::errc{}) {
        return {};
      }
      text.resize(static_cast<std::size_t>(result.ptr - text.data()));
      if (auto encoded = encode_text(entry.type, entry.endian, text, nullptr)) {
        return std::move(encoded->bytes);
      }
      return {};
    }
  }
  if (auto encoded =
          encode_i64(entry.type, entry.endian, number.integer, nullptr)) {
    return std::move(encoded->bytes);
  }
  return {};
}

Number clamp_number(const Number &current, const Number *min,
                    const Number *max) {
  Number value = current;
  if (min != nullptr && compare_numbers(value, *min) < 0) {
    value = *min;
  }
  if (max != nullptr && compare_numbers(value, *max) > 0) {
    value = *max;
  }
  return value;
}

} // namespace

void Freezer::tick(const ProcessMemory &memory, AddressTable &table) const {
  if (!memory.alive()) {
    return;
  }
  for (AddressEntry &entry : table.entries()) {
    if (!entry.frozen || entry.frozen_value.empty() || !entry.resolved) {
      continue;
    }
    if (!is_fixed_width(entry.type)) {
      continue;
    }
    std::vector<std::byte> current(entry.frozen_value.size());
    const auto io = memory.read(entry.address, current);
    if (!io.ok() || io.transferred != current.size()) {
      continue;
    }
    if (entry.freeze_mode == FreezeMode::Normal) {
      if (current != entry.frozen_value) {
        memory.write(entry.address, entry.frozen_value);
      }
      continue;
    }
    const auto now = decode_number(entry.type, entry.endian, current);
    const auto locked =
        decode_number(entry.type, entry.endian, entry.frozen_value);
    if (!now || !locked) {
      continue;
    }
    if (entry.freeze_mode == FreezeMode::AllowIncrease) {
      if (compare_numbers(*now, *locked) > 0) {
        entry.frozen_value = current;
      } else if (current != entry.frozen_value) {
        memory.write(entry.address, entry.frozen_value);
      }
      continue;
    }
    if (entry.freeze_mode == FreezeMode::AllowDecrease) {
      if (compare_numbers(*now, *locked) < 0) {
        entry.frozen_value = current;
      } else if (current != entry.frozen_value) {
        memory.write(entry.address, entry.frozen_value);
      }
      continue;
    }
    if (entry.freeze_mode == FreezeMode::Clamp) {
      const auto min = bound_number(entry, entry.clamp_min);
      const auto max = bound_number(entry, entry.clamp_max);
      if (!min && !max) {
        continue;
      }
      const Number clamped =
          clamp_number(*now, min ? &*min : nullptr, max ? &*max : nullptr);
      if (compare_numbers(clamped, *now) == 0) {
        continue;
      }
      const auto replacement = encode_from_number(entry, clamped);
      if (!replacement.empty()) {
        memory.write(entry.address, replacement);
      }
    }
  }
}

} // namespace gamex
