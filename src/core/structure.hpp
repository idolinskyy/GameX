#pragma once

#include "core/value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gamex {

struct StructureField {
  std::size_t offset = 0;
  ValueType type = ValueType::Int32;
  std::string description;
  EncodedValue value;
};

struct StructureTemplate {
  std::string name;
  Endian endian = Endian::Little;
  std::vector<StructureField> fields;

  std::size_t span() const;
  bool validate(std::string* error) const;
};

struct StructureDraftSource {
  std::uintptr_t address = 0;
  ValueType type = ValueType::Int32;
  Endian endian = Endian::Little;
  std::string description;
  EncodedValue value;
};

// Чорновик з кількох адрес: найменша стає базою, інші — зсуви.
std::optional<StructureTemplate> draft_structure(std::string_view name,
                                                 const std::vector<StructureDraftSource>& sources,
                                                 std::string* error);

bool structure_matches(const StructureTemplate& tmpl, const std::byte* data, std::size_t available);

}  // namespace gamex
