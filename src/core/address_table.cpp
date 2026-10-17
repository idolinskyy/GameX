#include "core/address_table.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

namespace gamex {
namespace {

std::string hex_address(std::uintptr_t address) {
    std::string text(2 + sizeof(address) * 2, '\0');
    text[0] = '0';
    text[1] = 'x';
    const auto result = std::to_chars(text.data() + 2, text.data() + text.size(), address, 16);
    if (result.ec != std::errc{}) {
        return "0x0";
    }
    text.resize(static_cast<std::size_t>(result.ptr - text.data()));
    return text;
}

std::optional<std::uintptr_t> parse_address(std::string_view text) {
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(value);
}

std::string one_line(std::string_view value) {
    std::string text(value);
    for (char& ch : text) {
        if (ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return text;
}

std::string field(std::string_view key, std::string_view value) {
    const std::string flat = one_line(value);
    std::string line;
    line.reserve(key.size() + flat.size() + 2);
    line.append(key);
    line.append(": ");
    line.append(flat);
    return line;
}

std::string clamp_field(std::string_view text) {
    return text.empty() ? "-" : std::string(text);
}

std::string clamp_from_file(std::string_view text) {
    if (text.empty() || text == "-") {
        return {};
    }
    return std::string(text);
}

std::string offsets_to_text(const std::vector<std::int64_t>& offsets) {
    if (offsets.empty()) {
        return "-";
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << offsets[i];
    }
    return out.str();
}

std::vector<std::int64_t> offsets_from_text(std::string_view text) {
    std::vector<std::int64_t> offsets;
    if (text.empty() || text == "-") {
        return offsets;
    }
    std::size_t start = 0;
    while (start < text.size()) {
        const auto comma = text.find(',', start);
        const auto part = text.substr(start, comma == std::string_view::npos ? text.npos : comma - start);
        std::int64_t value = 0;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        if (result.ec == std::errc{} && result.ptr == part.data() + part.size()) {
            offsets.push_back(value);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return offsets;
}

}  // namespace

std::string AddressEntry::binding_text() const {
    if (has_emulator()) {
        return emulator + "@" + hex_address(guest_address);
    }
    if (!has_binding()) {
        return "абс.";
    }
    return format_binding(module, static_offset, pointer_offsets);
}

bool bind_to_module(AddressEntry& entry, const ModuleMap& modules) {
    const ModuleInfo* module = modules.find_containing(entry.address);
    if (module == nullptr) {
        return false;
    }
    entry.module = module->name;
    entry.static_offset = entry.address - module->base;
    entry.pointer_offsets.clear();
    entry.emulator.clear();
    entry.guest_address = 0;
    entry.resolved = true;
    return true;
}

bool resolve_entry(AddressEntry& entry, const ProcessMemory& memory, const ModuleMap& modules,
                   std::string* error) {
    if (entry.has_emulator()) {
        const EmulatorProfile* profile = find_emulator_profile(entry.emulator);
        if (profile == nullptr) {
            if (error) {
                *error = "Невідомий профіль емулятора: " + entry.emulator;
            }
            entry.resolved = false;
            return false;
        }
        const auto mapping = locate_guest_ram(memory, *profile, error);
        if (!mapping) {
            entry.resolved = false;
            return false;
        }
        const auto host = guest_to_host(*mapping, *profile, entry.guest_address);
        if (!host) {
            if (error) {
                *error = "Гостьова адреса поза знайденим блоком RAM";
            }
            entry.resolved = false;
            return false;
        }
        entry.address = *host;
        entry.resolved = true;
        return true;
    }
    if (!entry.has_binding()) {
        entry.resolved = true;
        return true;
    }
    const auto resolved = PointerScanner::resolve(memory, modules, entry.module, entry.static_offset,
                                                  entry.pointer_offsets, error);
    if (!resolved) {
        entry.resolved = false;
        return false;
    }
    entry.address = *resolved;
    entry.resolved = true;
    return true;
}

std::string_view freeze_mode_label(FreezeMode mode) {
    switch (mode) {
        case FreezeMode::Normal:
            return "Утримати";
        case FreezeMode::AllowIncrease:
            return "Може зрости";
        case FreezeMode::AllowDecrease:
            return "Може спасти";
        case FreezeMode::Clamp:
            return "У межах";
    }
    return "Утримати";
}

std::optional<FreezeMode> freeze_mode_from_label(std::string_view label) {
    constexpr FreezeMode kModes[] = {FreezeMode::Normal, FreezeMode::AllowIncrease,
                                     FreezeMode::AllowDecrease, FreezeMode::Clamp};
    for (FreezeMode mode : kModes) {
        if (freeze_mode_label(mode) == label || freeze_mode_name(mode) == label) {
            return mode;
        }
    }
    return std::nullopt;
}

void AddressTable::add(AddressEntry entry) {
    entries_.push_back(std::move(entry));
}

void AddressTable::remove_at(std::size_t index) {
    if (index >= entries_.size()) {
        return;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
}

void AddressTable::clear() {
    entries_.clear();
}

std::size_t AddressTable::resolve_all(const ProcessMemory& memory, const ModuleMap& modules) {
    std::size_t failed = 0;
    for (AddressEntry& entry : entries_) {
        if (!resolve_entry(entry, memory, modules, nullptr)) {
            ++failed;
        }
    }
    return failed;
}

bool AddressTable::save(const std::string& path, std::string* error) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        if (error) {
            *error = "Не вдалося відкрити файл для запису";
        }
        return false;
    }
    output << "gamex-table 1\n";
    for (const AddressEntry& entry : entries_) {
        output << "---\n";
        output << field("description", entry.description) << '\n';
        output << field("address", hex_address(entry.address)) << '\n';
        output << field("type", value_type_name(entry.type)) << '\n';
        output << field("endian", endian_name(entry.endian)) << '\n';
        output << field("frozen", entry.frozen ? "1" : "0") << '\n';
        output << field("freeze_mode", freeze_mode_name(entry.freeze_mode)) << '\n';
        output << field("value", decode_text(entry.type, entry.endian, entry.frozen_value)) << '\n';
        output << field("clamp_min", clamp_field(entry.clamp_min)) << '\n';
        output << field("clamp_max", clamp_field(entry.clamp_max)) << '\n';
        output << field("group", entry.group) << '\n';
        output << field("module", entry.module.empty() ? "-" : entry.module) << '\n';
        output << field("static_offset", entry.module.empty() ? "-" : hex_address(entry.static_offset))
               << '\n';
        output << field("pointer_offsets", offsets_to_text(entry.pointer_offsets)) << '\n';
        output << field("emulator", entry.emulator.empty() ? "-" : entry.emulator) << '\n';
        output << field("guest_address",
                        entry.emulator.empty() ? "-" : hex_address(entry.guest_address))
               << '\n';
    }
    if (!output) {
        if (error) {
            *error = "Помилка запису таблиці";
        }
        return false;
    }
    return true;
}

bool AddressTable::load(const std::string& path, std::string* error) {
    std::ifstream input(path);
    if (!input) {
        if (error) {
            *error = "Не вдалося відкрити файл";
        }
        return false;
    }
    std::string magic;
    if (!std::getline(input, magic) || magic != "gamex-table 1") {
        if (error) {
            *error = "Файл таблиці пошкоджено";
        }
        return false;
    }

    std::vector<AddressEntry> loaded;
    AddressEntry current;
    bool in_entry = false;
    auto finish = [&]() {
        if (!in_entry) {
            return;
        }
        loaded.push_back(std::move(current));
        current = AddressEntry{};
        in_entry = false;
    };

    std::string line;
    std::string pending_value;
    auto flush_value = [&](AddressEntry& entry) {
        if (pending_value.empty()) {
            return;
        }
        std::string encode_error;
        if (auto encoded = encode_text(entry.type, entry.endian, pending_value, &encode_error)) {
            entry.frozen_value = std::move(encoded->bytes);
        }
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "---") {
            if (in_entry) {
                flush_value(current);
                pending_value.clear();
            }
            finish();
            in_entry = true;
            continue;
        }
        if (!in_entry) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            if (error) {
                *error = "Файл таблиці пошкоджено";
            }
            return false;
        }
        const std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        if (key == "description") {
            current.description = value;
        } else if (key == "address") {
            const auto address = parse_address(value);
            if (!address) {
                if (error) {
                    *error = "Некоректна адреса в таблиці";
                }
                return false;
            }
            current.address = *address;
        } else if (key == "type") {
            const auto type = value_type_from_name(value);
            if (!type) {
                if (error) {
                    *error = "Невідомий тип у таблиці";
                }
                return false;
            }
            current.type = *type;
        } else if (key == "endian") {
            const auto endian = endian_from_name(value);
            if (!endian) {
                if (error) {
                    *error = "Невідомий порядок байтів у таблиці";
                }
                return false;
            }
            current.endian = *endian;
        } else if (key == "frozen") {
            current.frozen = value == "1";
        } else if (key == "freeze_mode") {
            const auto mode = freeze_mode_from_name(value);
            if (!mode) {
                if (error) {
                    *error = "Невідомий режим заморозки";
                }
                return false;
            }
            current.freeze_mode = *mode;
        } else if (key == "value") {
            pending_value = value;
        } else if (key == "clamp_min") {
            current.clamp_min = clamp_from_file(value);
        } else if (key == "clamp_max") {
            current.clamp_max = clamp_from_file(value);
        } else if (key == "group") {
            current.group = value;
        } else if (key == "module") {
            current.module = value == "-" ? std::string{} : value;
        } else if (key == "static_offset") {
            if (value != "-") {
                const auto offset = parse_address(value);
                if (!offset) {
                    if (error) {
                        *error = "Некоректний static_offset";
                    }
                    return false;
                }
                current.static_offset = *offset;
            }
        } else if (key == "pointer_offsets") {
            current.pointer_offsets = offsets_from_text(value);
        } else if (key == "emulator") {
            current.emulator = value == "-" ? std::string{} : value;
        } else if (key == "guest_address") {
            if (value != "-") {
                const auto guest = parse_address(value);
                if (!guest) {
                    if (error) {
                        *error = "Некоректна guest_address";
                    }
                    return false;
                }
                current.guest_address = *guest;
            }
        }
    }
    if (in_entry) {
        flush_value(current);
        finish();
    }
    entries_ = std::move(loaded);
    return true;
}

}  // namespace gamex
