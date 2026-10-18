#include "core/process_memory.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

namespace gamex {
namespace {

std::string read_file(const std::string &path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string trim_copy(std::string text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

std::string command_line_of(pid_t pid) {
  std::string raw = read_file("/proc/" + std::to_string(pid) + "/cmdline");
  for (char &ch : raw) {
    if (ch == '\0') {
      ch = ' ';
    }
  }
  return trim_copy(std::move(raw));
}

std::string comm_of(pid_t pid) {
  return trim_copy(read_file("/proc/" + std::to_string(pid) + "/comm"));
}

} // namespace

std::optional<unsigned long long> process_starttime(pid_t pid) {
  if (pid <= 0) {
    return std::nullopt;
  }
  const std::string raw = read_file("/proc/" + std::to_string(pid) + "/stat");
  if (raw.empty()) {
    return std::nullopt;
  }
  // Формат: pid (comm) state ... — comm може містити ')' і пробіли.
  const auto close = raw.rfind(')');
  if (close == std::string::npos || close + 2 >= raw.size()) {
    return std::nullopt;
  }
  std::size_t pos = close + 2;
  // Після ')': state + 19 полів до starttime (поле 22 загалом).
  for (int field = 0; field < 20; ++field) {
    while (pos < raw.size() && raw[pos] == ' ') {
      ++pos;
    }
    const auto next = raw.find(' ', pos);
    if (next == std::string::npos) {
      if (field != 19) {
        return std::nullopt;
      }
      unsigned long long value = 0;
      const auto parsed =
          std::from_chars(raw.data() + static_cast<std::ptrdiff_t>(pos),
                          raw.data() + static_cast<std::ptrdiff_t>(raw.size()),
                          value);
      if (parsed.ec != std::errc{}) {
        return std::nullopt;
      }
      return value;
    }
    if (field == 19) {
      unsigned long long value = 0;
      const auto parsed = std::from_chars(
          raw.data() + static_cast<std::ptrdiff_t>(pos),
          raw.data() + static_cast<std::ptrdiff_t>(next), value);
      if (parsed.ec != std::errc{}) {
        return std::nullopt;
      }
      return value;
    }
    pos = next + 1;
  }
  return std::nullopt;
}

ProcessMemory::ProcessMemory(pid_t pid) : pid_(pid) {
  if (auto start = process_starttime(pid)) {
    starttime_ = *start;
  }
}

std::string ProcessMemory::last_error() const {
  std::lock_guard lock(mutex_);
  return last_error_;
}

ProcessMemory::ProcessMemory(ProcessMemory &&other) noexcept
    : pid_(other.pid_), starttime_(other.starttime_) {
  other.pid_ = -1;
  other.starttime_ = 0;
}

ProcessMemory &ProcessMemory::operator=(ProcessMemory &&other) noexcept {
  if (this != &other) {
    std::lock_guard lock(mutex_);
    pid_ = other.pid_;
    starttime_ = other.starttime_;
    other.pid_ = -1;
    other.starttime_ = 0;
    last_error_.clear();
  }
  return *this;
}

bool ProcessMemory::alive() const {
  if (pid_ <= 0) {
    return false;
  }
  const auto current = process_starttime(pid_);
  if (!current) {
    return false;
  }
  if (starttime_ != 0 && *current != starttime_) {
    return false;
  }
  return true;
}

bool ProcessMemory::valid() const { return alive(); }

std::optional<ProcessMemory> ProcessMemory::open(pid_t pid,
                                                 std::string *error) {
  if (pid <= 0) {
    if (error) {
      *error = "Некоректний pid";
    }
    return std::nullopt;
  }
  ProcessMemory memory(pid);
  if (memory.starttime_ == 0) {
    if (error) {
      *error = "Процес не існує або недоступний /proc";
    }
    return std::nullopt;
  }
  if (!memory.alive()) {
    if (error) {
      *error = "Процес не існує";
    }
    return std::nullopt;
  }
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  if (!maps) {
    if (error) {
      *error = "Немає доступу до карти пам'яті процесу";
    }
    return std::nullopt;
  }
  return memory;
}

std::vector<MemoryRegion> ProcessMemory::regions() const {
  std::lock_guard lock(mutex_);
  last_error_.clear();
  std::ifstream maps("/proc/" + std::to_string(pid_) + "/maps");
  if (!maps) {
    last_error_ =
        "Не вдалося прочитати /proc/" + std::to_string(pid_) + "/maps";
    return {};
  }
  std::vector<MemoryRegion> result;
  std::string line;
  while (std::getline(maps, line)) {
    if (auto region = parse_maps_line(line)) {
      result.push_back(std::move(*region));
    }
  }
  return result;
}

IoResult ProcessMemory::read(std::uintptr_t address,
                             std::span<std::byte> buffer) const {
  std::lock_guard lock(mutex_);
  last_error_.clear();
  if (!alive() || buffer.empty()) {
    last_error_ = "Процес недоступний";
    return {.transferred = 0, .error = ESRCH};
  }
  iovec local{};
  local.iov_base = buffer.data();
  local.iov_len = buffer.size();
  iovec remote{};
  remote.iov_base = reinterpret_cast<void *>(address);
  remote.iov_len = buffer.size();
  const ssize_t n = ::process_vm_readv(pid_, &local, 1, &remote, 1, 0);
  if (n < 0) {
    last_error_ = "Читання пам'яті відхилено";
    return {.transferred = 0, .error = errno};
  }
  return {.transferred = static_cast<std::size_t>(n), .error = 0};
}

IoResult ProcessMemory::write(std::uintptr_t address,
                              std::span<const std::byte> buffer) const {
  std::lock_guard lock(mutex_);
  last_error_.clear();
  if (!alive() || buffer.empty()) {
    last_error_ = "Процес недоступний";
    return {.transferred = 0, .error = ESRCH};
  }
  iovec local{};
  local.iov_base = const_cast<std::byte *>(buffer.data());
  local.iov_len = buffer.size();
  iovec remote{};
  remote.iov_base = reinterpret_cast<void *>(address);
  remote.iov_len = buffer.size();
  const ssize_t n = ::process_vm_writev(pid_, &local, 1, &remote, 1, 0);
  if (n < 0) {
    last_error_ = "Запис пам'яті відхилено";
    return {.transferred = 0, .error = errno};
  }
  return {.transferred = static_cast<std::size_t>(n), .error = 0};
}

std::vector<ProcessInfo> list_processes() {
  std::vector<ProcessInfo> processes;
  std::error_code error;
  for (const auto &entry :
       std::filesystem::directory_iterator("/proc", error)) {
    if (error) {
      break;
    }
    const std::string filename = entry.path().filename().string();
    if (filename.empty() ||
        !std::all_of(filename.begin(), filename.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      continue;
    }
    pid_t pid = -1;
    const auto parsed = std::from_chars(filename.data(),
                                        filename.data() + filename.size(), pid);
    if (parsed.ec != std::errc{} || pid <= 0) {
      continue;
    }
    ProcessInfo info;
    info.pid = pid;
    info.command_line = command_line_of(pid);
    info.name = comm_of(pid);
    if (info.name.empty()) {
      info.name = info.command_line;
    }
    processes.push_back(std::move(info));
  }
  std::ranges::sort(processes,
                    [](const ProcessInfo &left, const ProcessInfo &right) {
                      if (left.name != right.name) {
                        return left.name < right.name;
                      }
                      return left.pid < right.pid;
                    });
  return processes;
}

std::string executable_path(pid_t pid) {
  const std::string link = "/proc/" + std::to_string(pid) + "/exe";
  std::string buffer(4096, '\0');
  const ssize_t length =
      ::readlink(link.c_str(), buffer.data(), buffer.size() - 1);
  if (length < 0) {
    return {};
  }
  buffer.resize(static_cast<std::size_t>(length));
  return buffer;
}

std::string ptrace_scope() {
  std::ifstream input("/proc/sys/kernel/yama/ptrace_scope");
  std::string value;
  input >> value;
  return value;
}

} // namespace gamex
