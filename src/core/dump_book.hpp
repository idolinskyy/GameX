#pragma once

#include "core/process_memory.hpp"
#include "core/scanner.hpp"
#include "core/value_codec.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace gamex {

inline constexpr int kDumpSlots = 4;

class DumpBook {
public:
    void clear();
    void clear_slot(int slot);
    bool capture_hits(int slot, const Scanner& scanner, const ProcessMemory& memory,
                      ValueType type, Endian endian, std::string* error);
    bool has_slot(int slot) const;
    std::size_t size(int slot) const;

    // Відсів поточного списку збігів формулою з M1..M4.
    ScanStatus refine_formula(Scanner& scanner, const ProcessMemory& memory, ValueType type,
                              Endian endian, std::string_view formula, std::string* error) const;

private:
    std::array<std::unordered_map<std::uintptr_t, double>, kDumpSlots> slots_{};
};

}  // namespace gamex
