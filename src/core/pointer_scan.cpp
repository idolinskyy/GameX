#include "core/pointer_scan.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_set>

#include <errno.h>

namespace gamex {
namespace {

constexpr std::size_t kPointerWidth = sizeof(std::uintptr_t);

bool keep_going(const ScanControl& control, std::uint64_t scanned, std::uint64_t total) {
    return !control || control(scanned, total);
}

bool file_backed_static(const MemoryRegion& region) {
    return !region.pathname.empty() && region.pathname.front() == '/' && region.readable &&
           !region.special_kernel();
}

bool writable_dynamic(const MemoryRegion& region) {
    if (!region.scannable(true)) {
        return false;
    }
    if (region.pathname.empty() || region.pathname == "[heap]" || region.pathname == "[stack]") {
        return true;
    }
    // Анонімні та купові відображення; файлові модулі лишаємо для статичного проходу.
    return region.pathname.front() != '/';
}

std::optional<std::uintptr_t> read_pointer(const ProcessMemory& memory, std::uintptr_t address) {
    std::uintptr_t value = 0;
    const auto io =
        memory.read(address, std::span<std::byte>(reinterpret_cast<std::byte*>(&value), sizeof(value)));
    if (!io.ok() || io.transferred != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

void collect_equals(ProcessMemory& memory, const std::vector<MemoryRegion>& regions, bool static_only,
                    std::uintptr_t needle, std::vector<std::uintptr_t>& out, std::size_t limit,
                    const ScanControl& control, bool& cancelled, bool& denied) {
    constexpr std::size_t kChunk = 1u << 20;
    std::vector<std::byte> window(kChunk + kPointerWidth);
    std::uint64_t scanned = 0;
    std::uint64_t total = 0;
    for (const MemoryRegion& region : regions) {
        const bool accept = static_only ? file_backed_static(region) : writable_dynamic(region);
        if (!accept || region.size() < kPointerWidth) {
            continue;
        }
        total += region.size();
    }
    for (const MemoryRegion& region : regions) {
        const bool accept = static_only ? file_backed_static(region) : writable_dynamic(region);
        if (!accept || region.size() < kPointerWidth) {
            continue;
        }
        std::size_t carry = 0;
        std::uintptr_t cursor = region.start;
        while (cursor < region.end && out.size() < limit) {
            if (!keep_going(control, scanned, total)) {
                cancelled = true;
                return;
            }
            const std::size_t want =
                std::min<std::size_t>(kChunk, static_cast<std::size_t>(region.end - cursor));
            const auto io = memory.read(cursor, std::span<std::byte>(window.data() + carry, want));
            if (io.error == EPERM || io.error == EACCES) {
                denied = true;
                break;
            }
            if (io.transferred == 0) {
                break;
            }
            const std::size_t total_bytes = carry + io.transferred;
            const std::uintptr_t base = cursor - carry;
            if (total_bytes >= kPointerWidth) {
                const std::uintptr_t aligned =
                    base + ((kPointerWidth - (base % kPointerWidth)) % kPointerWidth);
                for (std::uintptr_t address = aligned;
                     address + kPointerWidth <= base + total_bytes && out.size() < limit;
                     address += kPointerWidth) {
                    std::uintptr_t value = 0;
                    std::memcpy(&value, window.data() + (address - base), sizeof(value));
                    if (value == needle) {
                        out.push_back(address);
                    }
                }
            }
            carry = total_bytes >= kPointerWidth - 1 ? kPointerWidth - 1 : 0;
            if (carry != 0) {
                std::memmove(window.data(), window.data() + (total_bytes - carry), carry);
            }
            cursor += io.transferred;
            scanned += io.transferred;
            if (io.transferred < want) {
                break;
            }
        }
        if (cancelled || out.size() >= limit) {
            return;
        }
    }
}

void append_static_paths(const ModuleMap& modules, const std::vector<std::uintptr_t>& static_hits,
                         const std::vector<std::int64_t>& tail_offsets, std::uintptr_t target,
                         std::vector<PointerPath>& paths, bool& truncated) {
    for (std::uintptr_t address : static_hits) {
        const ModuleInfo* module = modules.find_containing(address);
        if (module == nullptr) {
            continue;
        }
        PointerPath path;
        path.module = module->name;
        path.static_offset = address - module->base;
        path.offsets = tail_offsets;
        path.resolved = target;
        paths.push_back(std::move(path));
        if (paths.size() >= kMaxPointerPaths) {
            truncated = true;
            return;
        }
    }
}

std::string hex_offset(std::uintptr_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

}  // namespace

PointerScanner::PointerScanner(ProcessMemory& memory) : memory_(&memory) {}

std::optional<std::uintptr_t> PointerScanner::resolve(const ProcessMemory& memory, const ModuleMap& modules,
                                                      std::string_view module_name,
                                                      std::uintptr_t static_offset,
                                                      const std::vector<std::int64_t>& offsets,
                                                      std::string* error) {
    const auto base = modules.resolve_static(module_name, static_offset);
    if (!base) {
        if (error) {
            *error = "Модуль не знайдено: " + std::string(module_name);
        }
        return std::nullopt;
    }
    std::uintptr_t address = *base;
    for (std::int64_t step : offsets) {
        const auto pointer = read_pointer(memory, address);
        if (!pointer) {
            if (error) {
                *error = "Не вдалося прочитати вказівник у ланцюжку";
            }
            return std::nullopt;
        }
        if (step >= 0) {
            address = *pointer + static_cast<std::uintptr_t>(step);
        } else {
            address = *pointer - static_cast<std::uintptr_t>(-step);
        }
    }
    return address;
}

PointerScanStatus PointerScanner::scan(std::uintptr_t target, int max_depth, const ScanControl& control) {
    paths_.clear();
    PointerScanStatus status;
    if (memory_ == nullptr || !memory_->alive()) {
        status.message = "Процес недоступний";
        return status;
    }
    if (max_depth < 1 || max_depth > kMaxPointerDepth) {
        status.message = "Глибина має бути від 1 до 4";
        return status;
    }

    const auto regions = memory_->regions();
    const ModuleMap modules = ModuleMap::from_regions(regions);
    bool cancelled = false;
    bool denied = false;
    bool truncated = false;

    // current[i] — адреса комірки, з якої ланцюжок offsets[i] веде до target.
    std::vector<std::uintptr_t> current{target};
    std::vector<std::vector<std::int64_t>> offsets(1);

    for (int depth = 1; depth <= max_depth && !cancelled && !truncated; ++depth) {
        std::unordered_set<std::uintptr_t> seen_static;
        for (std::size_t index = 0; index < current.size() && !cancelled && !truncated; ++index) {
            std::vector<std::uintptr_t> static_hits;
            collect_equals(*memory_, regions, true, current[index], static_hits, 512, control, cancelled,
                           denied);
            std::vector<std::int64_t> chain = offsets[index];
            chain.insert(chain.begin(), 0);
            std::vector<std::uintptr_t> unique;
            unique.reserve(static_hits.size());
            for (std::uintptr_t address : static_hits) {
                if (seen_static.insert(address).second) {
                    unique.push_back(address);
                }
            }
            append_static_paths(modules, unique, chain, target, paths_, truncated);
        }

        if (depth == max_depth || truncated || cancelled) {
            break;
        }

        std::vector<std::uintptr_t> next;
        std::vector<std::vector<std::int64_t>> next_offsets;
        for (std::size_t index = 0; index < current.size() && !cancelled; ++index) {
            std::vector<std::uintptr_t> holders;
            collect_equals(*memory_, regions, false, current[index], holders, 8192, control, cancelled,
                           denied);
            for (std::uintptr_t holder : holders) {
                next.push_back(holder);
                auto chain = offsets[index];
                chain.insert(chain.begin(), 0);
                next_offsets.push_back(std::move(chain));
                if (next.size() >= kMaxPointerIntermediates) {
                    truncated = true;
                    break;
                }
            }
            if (truncated) {
                break;
            }
        }
        current = std::move(next);
        offsets = std::move(next_offsets);
        if (current.empty()) {
            break;
        }
    }

    if (denied && paths_.empty()) {
        status.message = "Немає прав читати пам'ять для пошуку вказівників";
        return status;
    }
    status.ok = true;
    status.cancelled = cancelled;
    status.truncated = truncated;
    status.path_count = paths_.size();
    if (cancelled) {
        status.message = "Пошук вказівників зупинено";
    } else if (paths_.empty()) {
        status.message =
            "Статичних вказівників не знайдено. Якщо адреса збирається лише в рантаймі і ніде не "
            "лежить у модулі, цей пошук її не знайде.";
    } else if (truncated) {
        status.message = "Досягнуто ліміт шляхів";
    }
    return status;
}

std::string format_pointer_path(const PointerPath& path) {
    return format_binding(path.module, path.static_offset, path.offsets);
}

std::string format_binding(std::string_view module, std::uintptr_t static_offset,
                           const std::vector<std::int64_t>& offsets) {
    std::ostringstream out;
    if (module.empty()) {
        out << "абс.";
        return out.str();
    }
    if (offsets.empty()) {
        out << module << '+' << hex_offset(static_offset);
        return out.str();
    }
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        out << '[';
    }
    out << module << '+' << hex_offset(static_offset);
    for (std::int64_t step : offsets) {
        out << ']';
        if (step > 0) {
            out << '+' << hex_offset(static_cast<std::uintptr_t>(step));
        } else if (step < 0) {
            out << '-' << hex_offset(static_cast<std::uintptr_t>(-step));
        }
    }
    return out.str();
}

}  // namespace gamex
