#pragma once

#include "core/module_map.hpp"
#include "core/process_memory.hpp"
#include "core/scanner.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace gamex {

inline constexpr int kMaxPointerDepth = 4;
inline constexpr std::size_t kMaxPointerPaths = 5000;
inline constexpr std::size_t kMaxPointerIntermediates = 200'000;

struct PointerPath {
    std::string module;
    std::uintptr_t static_offset = 0;
    std::vector<std::int64_t> offsets;
    std::uintptr_t resolved = 0;
};

struct PointerScanStatus {
    bool ok = false;
    bool truncated = false;
    bool cancelled = false;
    std::size_t path_count = 0;
    std::string message;
};

class PointerScanner {
public:
    explicit PointerScanner(ProcessMemory& memory);

    PointerScanStatus scan(std::uintptr_t target, int max_depth, const ScanControl& control = {});
    const std::vector<PointerPath>& paths() const { return paths_; }

    static std::optional<std::uintptr_t> resolve(const ProcessMemory& memory, const ModuleMap& modules,
                                                 std::string_view module_name, std::uintptr_t static_offset,
                                                 const std::vector<std::int64_t>& offsets, std::string* error);

private:
    ProcessMemory* memory_ = nullptr;
    std::vector<PointerPath> paths_;
};

std::string format_pointer_path(const PointerPath& path);
std::string format_binding(std::string_view module, std::uintptr_t static_offset,
                           const std::vector<std::int64_t>& offsets);

}  // namespace gamex
