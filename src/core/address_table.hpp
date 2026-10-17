#pragma once

#include "core/emulator_profile.hpp"
#include "core/module_map.hpp"
#include "core/pointer_scan.hpp"
#include "core/process_memory.hpp"
#include "core/value_codec.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gamex {

struct AddressEntry {
    std::string description;
    std::uintptr_t address = 0;
    ValueType type = ValueType::Int32;
    Endian endian = Endian::Little;
    bool frozen = false;
    FreezeMode freeze_mode = FreezeMode::Normal;
    std::vector<std::byte> frozen_value;
    std::string clamp_min;
    std::string clamp_max;
    std::string group;

    // Порожній module — абсолютна адреса. Інакше base = module+static_offset,
    // далі для кожного pointer_offsets[i]: addr = *addr + pointer_offsets[i].
    std::string module;
    std::uintptr_t static_offset = 0;
    std::vector<std::int64_t> pointer_offsets;

    // Профіль емулятора: адреса в таблиці — гостьова (guest_address), host у address після resolve.
    std::string emulator;
    std::uintptr_t guest_address = 0;

    bool resolved = true;

    bool has_binding() const { return !module.empty(); }
    bool has_emulator() const { return !emulator.empty(); }
    std::string binding_text() const;
};

std::string_view freeze_mode_label(FreezeMode mode);
std::optional<FreezeMode> freeze_mode_from_label(std::string_view label);

bool bind_to_module(AddressEntry& entry, const ModuleMap& modules);
bool resolve_entry(AddressEntry& entry, const ProcessMemory& memory, const ModuleMap& modules,
                   std::string* error);

class AddressTable {
public:
    const std::vector<AddressEntry>& entries() const { return entries_; }
    std::vector<AddressEntry>& entries() { return entries_; }

    void add(AddressEntry entry);
    void remove_at(std::size_t index);
    void clear();

    std::size_t resolve_all(const ProcessMemory& memory, const ModuleMap& modules);

    bool save(const std::string& path, std::string* error) const;
    bool load(const std::string& path, std::string* error);

private:
    std::vector<AddressEntry> entries_;
};

}  // namespace gamex
