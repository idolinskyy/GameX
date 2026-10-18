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

GuestRamMapping mapping_from(const MemoryRegion &region) {
  GuestRamMapping map;
  map.host_base = region.start;
  map.host_end = region.end;
  map.pathname = region.pathname;
  return map;
}

std::vector<const MemoryRegion *>
collect_candidates(const ProcessMemory &memory, const EmulatorProfile &profile,
                   bool require_overlap, std::uintptr_t prefer_start,
                   std::uintptr_t prefer_end, bool require_path) {
  std::vector<const MemoryRegion *> candidates;
  for (const MemoryRegion &region : memory.regions()) {
    if (!region.readable || !region.writable) {
      continue;
    }
    if (region.special_kernel()) {
      continue;
    }
    if (require_path && !path_matches(region, profile)) {
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
}

void sort_candidates(std::vector<const MemoryRegion *> &candidates,
                     const EmulatorProfile &profile) {
  std::sort(candidates.begin(), candidates.end(),
            [&](const MemoryRegion *a, const MemoryRegion *b) {
              const auto da = size_distance(*a, profile);
              const auto db = size_distance(*b, profile);
              if (da != db) {
                return da < db;
              }
              const bool a_named = !a->pathname.empty();
              const bool b_named = !b->pathname.empty();
              if (a_named != b_named) {
                return a_named;
              }
              return a->start < b->start;
            });
}

} // namespace

std::vector<EmulatorProfile> builtin_emulator_profiles() {
  std::vector<EmulatorProfile> list;
  // path_contains — м'яка підказка; якщо нічого не знайдено, пошук без фільтра.
  list.push_back(
      {"nes", "NES (2 КіБ WRAM)", "mesen", 2ull * 1024, 0, 0x0000, Endian::Little});
  list.push_back({"snes", "SNES (128 КіБ WRAM)", "snes9x", 128ull * 1024, 4096,
                  0x7E0000, Endian::Little});
  list.push_back({"gba", "GBA (256 КіБ EWRAM)", "mgba", 256ull * 1024, 0,
                  0x02000000, Endian::Little});
  list.push_back({"nds", "NDS (4 МіБ main RAM)", "melon", 4ull * 1024 * 1024, 0,
                  0x02000000, Endian::Little});
  list.push_back({"ps1", "PS1 (2 МіБ RAM)", "duckstation", 2ull * 1024 * 1024, 0,
                  0x80000000, Endian::Little});
  list.push_back({"n64", "N64 (4 МіБ RDRAM)", "mupen", 4ull * 1024 * 1024, 0,
                  0x80000000, Endian::Big});
  list.push_back({"n64_8m", "N64 (8 МіБ RDRAM)", "mupen", 8ull * 1024 * 1024, 0,
                  0x80000000, Endian::Big});
  list.push_back({"genesis", "Mega Drive (64 КіБ 68k RAM)", "gens", 64ull * 1024,
                  0, 0xFF0000, Endian::Big});
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

std::vector<GuestRamMapping>
find_guest_ram_candidates(const ProcessMemory &memory,
                          const EmulatorProfile &profile,
                          std::uintptr_t prefer_start,
                          std::uintptr_t prefer_end) {
  const bool prefer = prefer_end > prefer_start;
  auto try_collect = [&](bool require_path) {
    auto candidates = prefer ? collect_candidates(memory, profile, true,
                                                  prefer_start, prefer_end,
                                                  require_path)
                             : collect_candidates(memory, profile, false, 0, 0,
                                                  require_path);
    if (candidates.empty() && prefer) {
      candidates = collect_candidates(memory, profile, false, 0, 0, require_path);
    }
    return candidates;
  };

  auto candidates = try_collect(!profile.path_contains.empty());
  // Якщо path hint нічого не дав — шукаємо без нього (RetroArch тощо).
  if (candidates.empty() && !profile.path_contains.empty()) {
    candidates = try_collect(false);
  }
  sort_candidates(candidates, profile);

  std::vector<GuestRamMapping> result;
  result.reserve(candidates.size());
  for (const MemoryRegion *region : candidates) {
    result.push_back(mapping_from(*region));
  }
  return result;
}

std::optional<GuestRamMapping> locate_guest_ram(const ProcessMemory &memory,
                                                const EmulatorProfile &profile,
                                                std::string *error,
                                                std::uintptr_t prefer_start,
                                                std::uintptr_t prefer_end) {
  const auto candidates = find_guest_ram_candidates(
      memory, profile, prefer_start, prefer_end);
  if (candidates.empty()) {
    if (error) {
      *error = "Не знайдено регіон гостьової RAM для профілю «" +
               profile.title +
               "». Завантажте гру в емуляторі або вкажіть блок кнопкою «Блок "
               "пам'яті».";
    }
    return std::nullopt;
  }
  return candidates.front();
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
