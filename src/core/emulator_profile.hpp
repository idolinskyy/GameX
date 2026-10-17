#pragma once

#include "core/process_memory.hpp"
#include "core/value_codec.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gamex {

struct EmulatorProfile {
  std::string id;
  std::string title;
  // Підрядок у pathname регіону (/proc/pid/maps). Порожньо — не фільтрувати за шляхом.
  std::string path_contains;
  // Очікуваний розмір блоку гостьової RAM (байти). 0 — без підказки.
  std::uint64_t size_hint = 0;
  std::uint64_t size_tolerance = 0;
  // Гостьова адреса першого байта знайденого блоку.
  std::uintptr_t guest_base = 0;
  Endian endian = Endian::Little;
};

struct GuestRamMapping {
  std::uintptr_t host_base = 0;
  std::uintptr_t host_end = 0;
  std::string pathname;
  std::uint64_t size() const { return host_end > host_base ? host_end - host_base : 0; }
};

std::vector<EmulatorProfile> builtin_emulator_profiles();
const EmulatorProfile* find_emulator_profile(std::string_view id);

// Шукає rw-регіон під профіль: path_contains, size_hint±tolerance.
// Якщо prefer_end > prefer_start — спочатку серед регіонів, що перетинають цей діапазон.
std::optional<GuestRamMapping> locate_guest_ram(const ProcessMemory& memory,
                                                const EmulatorProfile& profile,
                                                std::string* error,
                                                std::uintptr_t prefer_start = 0,
                                                std::uintptr_t prefer_end = 0);

std::optional<std::uintptr_t> guest_to_host(const GuestRamMapping& map,
                                            const EmulatorProfile& profile,
                                            std::uintptr_t guest_address);

std::optional<std::uintptr_t> host_to_guest(const GuestRamMapping& map,
                                            const EmulatorProfile& profile,
                                            std::uintptr_t host_address);

}  // namespace gamex
