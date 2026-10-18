#include "core/dump_book.hpp"

#include "core/expression.hpp"

#include <cctype>

namespace gamex {
namespace {

bool formula_references_slot(std::string_view formula, int slot) {
  if (slot < 0 || slot >= kDumpSlots) {
    return false;
  }
  const char needle[3] = {'M', static_cast<char>('1' + slot), '\0'};
  for (std::size_t i = 0; i + 1 < formula.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(formula[i]);
    const unsigned char b = static_cast<unsigned char>(formula[i + 1]);
    if (std::toupper(a) != 'M' || b != needle[1]) {
      continue;
    }
    if (i > 0) {
      const unsigned char prev = static_cast<unsigned char>(formula[i - 1]);
      if (std::isalnum(prev) || prev == '_') {
        continue;
      }
    }
    if (i + 2 < formula.size()) {
      const unsigned char next = static_cast<unsigned char>(formula[i + 2]);
      if (std::isalnum(next) || next == '_') {
        continue;
      }
    }
    return true;
  }
  return false;
}

} // namespace

void DumpBook::clear() {
  for (auto &slot : slots_) {
    slot.clear();
  }
}

void DumpBook::clear_slot(int slot) {
  if (slot < 0 || slot >= kDumpSlots) {
    return;
  }
  slots_[static_cast<std::size_t>(slot)].clear();
}

bool DumpBook::has_slot(int slot) const {
  return slot >= 0 && slot < kDumpSlots &&
         !slots_[static_cast<std::size_t>(slot)].empty();
}

std::size_t DumpBook::size(int slot) const {
  if (slot < 0 || slot >= kDumpSlots) {
    return 0;
  }
  return slots_[static_cast<std::size_t>(slot)].size();
}

bool DumpBook::capture_hits(int slot, const Scanner &scanner,
                            const ProcessMemory &memory, ValueType type,
                            Endian endian, std::string *error) {
  if (slot < 0 || slot >= kDumpSlots) {
    if (error) {
      *error = "Слот знімка має бути M1..M4";
    }
    return false;
  }
  if (scanner.hit_count() == 0) {
    if (error) {
      *error = "Немає збігів для знімка";
    }
    return false;
  }
  if (!is_fixed_width(type)) {
    if (error) {
      *error = "Знімок формули працює для числових типів";
    }
    return false;
  }
  auto &map = slots_[static_cast<std::size_t>(slot)];
  map.clear();
  const std::size_t width = fixed_width(type);
  std::vector<std::byte> bytes(width);
  for (std::size_t i = 0; i < scanner.hit_count(); ++i) {
    const ScanHit hit = scanner.hit_at(i);
    const auto io = memory.read(hit.address, bytes);
    if (!io.ok() || io.transferred != width) {
      continue;
    }
    const auto number = decode_number(type, endian, bytes);
    if (!number) {
      continue;
    }
    map[hit.address] =
        number->is_float
            ? number->real
            : (number->is_signed ? static_cast<double>(number->integer)
                                 : static_cast<double>(number->uinteger));
  }
  if (map.empty()) {
    if (error) {
      *error = "Не вдалося прочитати значення для знімка";
    }
    return false;
  }
  return true;
}

ScanStatus DumpBook::refine_formula(Scanner &scanner,
                                    const ProcessMemory &memory, ValueType type,
                                    Endian /*endian*/, std::string_view formula,
                                    std::string *error) const {
  ScanStatus status;
  if (scanner.hit_count() == 0) {
    status.message = "Спочатку виконайте пошук";
    return status;
  }
  if (!is_fixed_width(type)) {
    status.message = "Формула працює для числових типів";
    return status;
  }

  bool any_ref = false;
  for (int slot = 0; slot < kDumpSlots; ++slot) {
    if (!formula_references_slot(formula, slot)) {
      continue;
    }
    any_ref = true;
    if (slots_[static_cast<std::size_t>(slot)].empty()) {
      status.message =
          "Формула посилається на порожній знімок M" + std::to_string(slot + 1) +
          ". Спочатку збережіть його кнопкою M" + std::to_string(slot + 1) + ".";
      if (error) {
        *error = status.message;
      }
      return status;
    }
  }
  if (!any_ref) {
    status.message = "У формулі немає змінних M1..M4";
    if (error) {
      *error = status.message;
    }
    return status;
  }

  ExprVars probe;
  for (int slot = 0; slot < kDumpSlots; ++slot) {
    if (!formula_references_slot(formula, slot)) {
      continue;
    }
    probe.emplace("M" + std::to_string(slot + 1), 0.0);
  }
  if (!eval_bool(formula, probe, error)) {
    status.message =
        error && !error->empty() ? *error : "Некоректна формула";
    return status;
  }

  const std::size_t width = fixed_width(type);
  std::vector<ScanHit> kept;
  std::vector<std::byte> bytes(width);
  for (std::size_t i = 0; i < scanner.hit_count(); ++i) {
    const ScanHit hit = scanner.hit_at(i);
    ExprVars vars;
    bool missing = false;
    for (int slot = 0; slot < kDumpSlots; ++slot) {
      if (!formula_references_slot(formula, slot)) {
        continue;
      }
      const auto &map = slots_[static_cast<std::size_t>(slot)];
      const auto it = map.find(hit.address);
      if (it == map.end()) {
        missing = true;
        break;
      }
      vars.emplace("M" + std::to_string(slot + 1), it->second);
    }
    if (missing) {
      continue;
    }
    std::string local_error;
    const auto pass = eval_bool(formula, vars, &local_error);
    if (!pass || !*pass) {
      continue;
    }
    ScanHit copy = hit;
    const auto io = memory.read(hit.address, bytes);
    if (io.ok() && io.transferred == width) {
      copy.previous.assign(bytes.begin(), bytes.end());
    } else if (copy.previous.size() != width) {
      copy.previous.assign(width, std::byte{0});
    }
    kept.push_back(std::move(copy));
  }

  scanner.replace_hits(std::move(kept));
  status.ok = true;
  status.hit_count = scanner.hit_count();
  return status;
}

} // namespace gamex
