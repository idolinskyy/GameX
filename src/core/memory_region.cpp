#include "core/memory_region.hpp"

#include <cctype>
#include <charconv>
#include <sstream>
#include <string>

namespace gamex {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::uint64_t> parse_hex(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value, 16);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::size_t MemoryRegion::size() const {
    if (end <= start) {
        return 0;
    }
    return static_cast<std::size_t>(end - start);
}

bool MemoryRegion::special_kernel() const {
    return pathname == "[vvar]" || pathname == "[vdso]" || pathname == "[vsyscall]";
}

bool MemoryRegion::scannable(bool writable_only) const {
    if (!readable || special_kernel() || size() == 0) {
        return false;
    }
    if (writable_only && !writable) {
        return false;
    }
    return true;
}

std::optional<MemoryRegion> parse_maps_line(std::string_view line) {
    line = trim(line);
    if (line.empty()) {
        return std::nullopt;
    }

    std::istringstream input{std::string(line)};
    std::string range;
    std::string perms;
    std::string offset;
    std::string device;
    std::string inode_text;
    if (!(input >> range >> perms >> offset >> device >> inode_text)) {
        return std::nullopt;
    }
    std::string path;
    std::getline(input, path);
    path = std::string(trim(path));

    const auto dash = range.find('-');
    if (dash == std::string::npos || perms.size() < 4) {
        return std::nullopt;
    }
    const auto start = parse_hex(std::string_view(range).substr(0, dash));
    const auto end = parse_hex(std::string_view(range).substr(dash + 1));
    const auto file_offset = parse_hex(offset);
    if (!start || !end || !file_offset || *end < *start) {
        return std::nullopt;
    }

    std::uint64_t inode = 0;
    {
        const char* begin = inode_text.data();
        const char* last = inode_text.data() + inode_text.size();
        const auto parsed = std::from_chars(begin, last, inode, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != last) {
            return std::nullopt;
        }
    }

    MemoryRegion region;
    region.start = static_cast<std::uintptr_t>(*start);
    region.end = static_cast<std::uintptr_t>(*end);
    region.readable = perms[0] == 'r';
    region.writable = perms[1] == 'w';
    region.executable = perms[2] == 'x';
    region.private_mapping = perms[3] == 'p';
    region.offset = *file_offset;
    region.inode = inode;
    region.pathname = std::move(path);
    return region;
}

}  // namespace gamex
