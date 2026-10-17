#pragma once

#include "core/address_table.hpp"
#include "core/process_memory.hpp"

namespace gamex {

class Freezer {
public:
    void tick(const ProcessMemory& memory, AddressTable& table) const;
};

}  // namespace gamex
