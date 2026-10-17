#include "core/structure.hpp"

#include <algorithm>
#include <cstring>

namespace gamex {

std::size_t StructureTemplate::span() const {
  std::size_t end = 0;
  for (const StructureField& field : fields) {
    if (field.value.bytes.empty()) {
      continue;
    }
    end = std::max(end, field.offset + field.value.bytes.size());
  }
  return end;
}

bool StructureTemplate::validate(std::string* error) const {
  if (fields.empty()) {
    if (error) {
      *error = "Структура без полів";
    }
    return false;
  }
  for (const StructureField& field : fields) {
    if (!is_fixed_width(field.type) && field.type != ValueType::ByteArray &&
        field.type != ValueType::StringUtf8) {
      if (error) {
        *error = "Невідомий тип поля";
      }
      return false;
    }
    if (field.value.bytes.empty()) {
      if (error) {
        *error = "Поле без значення: " +
                 (field.description.empty() ? std::string("(без імені)") : field.description);
      }
      return false;
    }
    if (is_fixed_width(field.type) && field.value.bytes.size() != fixed_width(field.type)) {
      if (error) {
        *error = "Ширина значення не збігається з типом поля";
      }
      return false;
    }
  }
  if (span() == 0) {
    if (error) {
      *error = "Порожня структура";
    }
    return false;
  }
  return true;
}

std::optional<StructureTemplate> draft_structure(std::string_view name,
                                                 const std::vector<StructureDraftSource>& sources,
                                                 std::string* error) {
  if (sources.size() < 2) {
    if (error) {
      *error = "Для чорновика потрібні щонайменше два рядки таблиці";
    }
    return std::nullopt;
  }
  std::vector<StructureDraftSource> sorted = sources;
  std::sort(sorted.begin(), sorted.end(),
            [](const StructureDraftSource& a, const StructureDraftSource& b) {
              return a.address < b.address;
            });
  const std::uintptr_t base = sorted.front().address;
  constexpr std::uintptr_t kMaxSpan = 4096;
  if (sorted.back().address - base > kMaxSpan) {
    if (error) {
      *error = "Адреси надто далеко одна від одної (понад 4 КіБ)";
    }
    return std::nullopt;
  }

  StructureTemplate tmpl;
  tmpl.name = std::string(name);
  tmpl.endian = sorted.front().endian;
  for (const StructureDraftSource& src : sorted) {
    StructureField field;
    field.offset = static_cast<std::size_t>(src.address - base);
    field.type = src.type;
    field.description = src.description;
    field.value = src.value;
    if (field.value.bytes.empty()) {
      if (error) {
        *error = "Немає значення для поля «" + src.description + "»";
      }
      return std::nullopt;
    }
    tmpl.fields.push_back(std::move(field));
  }
  if (!tmpl.validate(error)) {
    return std::nullopt;
  }
  return tmpl;
}

bool structure_matches(const StructureTemplate& tmpl, const std::byte* data, std::size_t available) {
  const std::size_t need = tmpl.span();
  if (data == nullptr || available < need) {
    return false;
  }
  for (const StructureField& field : tmpl.fields) {
    const std::size_t width = field.value.bytes.size();
    if (field.offset + width > available) {
      return false;
    }
    if (std::memcmp(data + field.offset, field.value.bytes.data(), width) != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace gamex
