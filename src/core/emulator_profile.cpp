#include "core/emulator_profile.hpp"

#include <algorithm>

namespace gamex {
namespace {

bool path_matches(const MemoryRegion &region, const EmulatorProfile &profile) {
  if (profile.path_contains.empty()) {
    return true;
  }
  return region.pathname.find(profile.path_contains) != std::string::npos;
}

bool size_matches(const MemoryRegion &region, const EmulatorProfile &profile) {
  if (profile.size_hint == 0) {
    return true;
  }
  const auto size = region.size();
  if (size == profile.size_hint) {
    return true;
  }
  if (profile.size_tolerance == 0) {
    return false;
  }
  const auto lo = profile.size_hint > profile.size_tolerance
                      ? profile.size_hint - profile.size_tolerance
                      : 0;
  const auto hi = profile.size_hint + profile.size_tolerance;
  return size >= lo && size <= hi;
}

std::int64_t size_distance(const MemoryRegion &region,
                           const EmulatorProfile &profile) {
  if (profile.size_hint == 0) {
    return 0;
  }
  const auto size = static_cast<std::int64_t>(region.size());
  const auto hint = static_cast<std::int64_t>(profile.size_hint);
  return size >= hint ? size - hint : hint - size;
}

} // namespace

std::vector<EmulatorProfile> builtin_emulator_profiles() {
  std::vector<EmulatorProfile> list;
  list.push_back(
      {"nes", "NES (2 КіБ WRAM)", "", 2ull * 1024, 0, 0x0000, Endian::Little});
  list.push_back({"snes", "SNES (128 КіБ WRAM)", "", 128ull * 1024, 4096,
                  0x7E0000, Endian::Little});
  list.push_back({"gba", "GBA (256 КіБ EWRAM)", "", 256ull * 1024, 0,
                  0x02000000, Endian::Little});
  list.push_back({"nds", "NDS (4 МіБ main RAM)", "", 4ull * 1024 * 1024, 0,
                  0x02000000, Endian::Little});
  list.push_back({"ps1", "PS1 (2 МіБ RAM)", "", 2ull * 1024 * 1024, 0,
                  0x80000000, Endian::Little});
  list.push_back({"n64", "N64 (4 МіБ RDRAM)", "", 4ull * 1024 * 1024, 0,
                  0x80000000, Endian::Big});
  list.push_back({"n64_8m", "N64 (8 МіБ RDRAM)", "", 8ull * 1024 * 1024, 0,
                  0x80000000, Endian::Big});
  list.push_back({"genesis", "Mega Drive (64 КіБ 68k RAM)", "", 64ull * 1024, 0,
                  0xFF0000, Endian::Big});
  return list;
}

const EmulatorProfile *find_emulator_profile(std::string_view id) {
  static const std::vector<EmulatorProfile> kProfiles =
      builtin_emulator_profiles();
  for (const EmulatorProfile &profile : kProfiles) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

std::optional<GuestRamMapping> locate_guest_ram(const ProcessMemory &memory,
                                                const EmulatorProfile &profile,
                                                std::string *error,
                                                std::uintptr_t prefer_start,
                                                std::uintptr_t prefer_end) {
  const bool prefer = prefer_end > prefer_start;
  auto collect = [&](bool require_overlap) {
    std::vector<const MemoryRegion *> candidates;
    for (const MemoryRegion &region : memory.regions()) {
      if (!region.readable || !region.writable) {
        continue;
      }
      if (region.special_kernel()) {
        continue;
      }
      if (!path_matches(region, profile)) {
        continue;
      }
      if (!size_matches(region, profile)) {
        continue;
      }
      if (require_overlap) {
        if (region.end <= prefer_start || region.start >= prefer_end) {
          continue;
        }
      }
      candidates.push_back(&region);
    }
    return candidates;
  };

  auto candidates = prefer ? collect(true) : collect(false);
  if (candidates.empty() && prefer) {
    candidates = collect(false);
  }

  if (candidates.empty()) {
    if (error) {
      *error = "Не знайдено регіон гостьової RAM для профілю «" +
               profile.title +
               "». Завантажте гру в емуляторі або вкажіть блок кнопкою «Блок "
               "пам'яті».";
    }
    return std::nullopt;
  }

  std::sort(candidates.begin(), candidates.end(),
            [&](const MemoryRegion *a, const MemoryRegion *b) {
              const auto da = size_distance(*a, profile);
              const auto db = size_distance(*b, profile);
              if (da != db) {
                return da < db;
              }
              return a->start < b->start;
            });

  const MemoryRegion &best = *candidates.front();
  GuestRamMapping map;
  map.host_base = best.start;
  map.host_end = best.end;
  map.pathname = best.pathname;
  return map;
}

std::optional<std::uintptr_t> guest_to_host(const GuestRamMapping &map,
                                            const EmulatorProfile &profile,
                                            std::uintptr_t guest_address) {
  if (guest_address < profile.guest_base) {
    return std::nullopt;
  }
  const std::uintptr_t offset = guest_address - profile.guest_base;
  if (offset >= map.size()) {
    return std::nullopt;
  }
  return map.host_base + offset;
}

std::optional<std::uintptr_t> host_to_guest(const GuestRamMapping &map,
                                            const EmulatorProfile &profile,
                                            std::uintptr_t host_address) {
  if (host_address < map.host_base || host_address >= map.host_end) {
    return std::nullopt;
  }
  return profile.guest_base + (host_address - map.host_base);
}

} // namespace gamex
