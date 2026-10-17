#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gamex {

struct MemoryRegion {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
    bool private_mapping = false;
    std::uint64_t offset = 0;
    std::uint64_t inode = 0;
    std::string pathname;

    std::size_t size() const;
    bool special_kernel() const;
    bool scannable(bool writable_only) const;
};

std::optional<MemoryRegion> parse_maps_line(std::string_view line);

}  // namespace gamex
