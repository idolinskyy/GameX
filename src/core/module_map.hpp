#pragma once

#include "core/memory_region.hpp"
#include "core/process_memory.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gamex {

struct ModuleInfo {
    std::string name;
    std::string path;
    std::uintptr_t base = 0;
    std::uintptr_t end = 0;
};

class ModuleMap {
public:
    static ModuleMap from_regions(const std::vector<MemoryRegion>& regions);
    static ModuleMap from_process(const ProcessMemory& memory);

    const std::vector<ModuleInfo>& modules() const { return modules_; }

    const ModuleInfo* find_by_name(std::string_view name) const;
    const ModuleInfo* find_containing(std::uintptr_t address) const;

    std::optional<std::uintptr_t> resolve_static(std::string_view module_name,
                                                std::uintptr_t offset) const;

private:
    std::vector<ModuleInfo> modules_;
};

std::string module_basename(std::string_view path);

}  // namespace gamex
