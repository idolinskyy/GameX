#pragma once

#include "core/address_table.hpp"
#include "core/process_memory.hpp"

#include <cstddef>

namespace gamex {

struct FreezeTickStats {
  std::size_t active = 0;
  std::size_t read_failures = 0;
  std::size_t write_failures = 0;
};

class Freezer {
public:
  FreezeTickStats tick(const ProcessMemory &memory, AddressTable &table) const;
};

} // namespace gamex
