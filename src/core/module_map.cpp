#include "core/module_map.hpp"

#include <algorithm>

namespace gamex {
namespace {

bool is_file_backed(const MemoryRegion& region) {
    return !region.pathname.empty() && region.pathname.front() == '/' && !region.special_kernel();
}

}  // namespace

std::string module_basename(std::string_view path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string_view::npos) {
        return std::string(path);
    }
    return std::string(path.substr(slash + 1));
}

ModuleMap ModuleMap::from_regions(const std::vector<MemoryRegion>& regions) {
    ModuleMap map;
    for (const MemoryRegion& region : regions) {
        if (!is_file_backed(region)) {
            continue;
        }
        const std::string name = module_basename(region.pathname);
        if (name.empty()) {
            continue;
        }
        auto existing = std::find_if(map.modules_.begin(), map.modules_.end(),
                                     [&](const ModuleInfo& info) { return info.path == region.pathname; });
        if (existing == map.modules_.end()) {
            ModuleInfo info;
            info.name = name;
            info.path = region.pathname;
            info.base = region.start;
            info.end = region.end;
            map.modules_.push_back(std::move(info));
        } else {
            existing->base = std::min(existing->base, region.start);
            existing->end = std::max(existing->end, region.end);
        }
    }
    std::ranges::sort(map.modules_, [](const ModuleInfo& left, const ModuleInfo& right) {
        return left.base < right.base;
    });
    return map;
}

ModuleMap ModuleMap::from_process(const ProcessMemory& memory) {
    return from_regions(memory.regions());
}

const ModuleInfo* ModuleMap::find_by_name(std::string_view name) const {
    const ModuleInfo* best = nullptr;
    for (const ModuleInfo& info : modules_) {
        if (info.name == name || info.path == name) {
            if (best == nullptr || info.base < best->base) {
                best = &info;
            }
        }
    }
    return best;
}

const ModuleInfo* ModuleMap::find_containing(std::uintptr_t address) const {
    for (const ModuleInfo& info : modules_) {
        if (address >= info.base && address < info.end) {
            return &info;
        }
    }
    return nullptr;
}

std::optional<std::uintptr_t> ModuleMap::resolve_static(std::string_view module_name,
                                                        std::uintptr_t offset) const {
    const ModuleInfo* module = find_by_name(module_name);
    if (module == nullptr) {
        return std::nullopt;
    }
    return module->base + offset;
}

}  // namespace gamex
