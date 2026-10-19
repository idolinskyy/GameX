#include "core/process_memory.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/uio.h>
#include <unistd.h>

#if defined(GAMEX_HAS_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

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

std::optional<unsigned long> status_field_u64(const std::string &raw,
                                              std::string_view key) {
  const auto pos = raw.find(key);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t cursor = pos + key.size();
  while (cursor < raw.size() && (raw[cursor] == ' ' || raw[cursor] == '\t')) {
    ++cursor;
  }
  unsigned long value = 0;
  const auto parsed = std::from_chars(
      raw.data() + static_cast<std::ptrdiff_t>(cursor),
      raw.data() + static_cast<std::ptrdiff_t>(raw.size()), value);
  if (parsed.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

struct StatusIds {
  uid_t uid = static_cast<uid_t>(-1);
  pid_t tgid = -1;
};

StatusIds ids_from_status(pid_t pid) {
  StatusIds ids;
  const std::string raw = read_file("/proc/" + std::to_string(pid) + "/status");
  if (raw.empty()) {
    return ids;
  }
  // Uid:\treal\teffective\tsaved\tfs
  if (const auto uid = status_field_u64(raw, "Uid:")) {
    ids.uid = static_cast<uid_t>(*uid);
  }
  // Tgid: — id групи потоків (= pid лідера процесу).
  if (const auto tgid = status_field_u64(raw, "Tgid:")) {
    ids.tgid = static_cast<pid_t>(*tgid);
  } else {
    ids.tgid = pid;
  }
  return ids;
}

} // namespace

int process_thread_count(pid_t pid) {
  int count = 0;
  std::error_code error;
  const auto path =
      std::filesystem::path("/proc") / std::to_string(pid) / "task";
  for (const auto &entry : std::filesystem::directory_iterator(path, error)) {
    if (error) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (!name.empty() &&
        std::all_of(name.begin(), name.end(),
                    [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      ++count;
    }
  }
  return count > 0 ? count : 1;
}

std::vector<pid_t> list_shell_panel_pids() {
  std::vector<pid_t> result;
#if defined(GAMEX_HAS_X11)
  Display *display = ::XOpenDisplay(nullptr);
  if (display == nullptr) {
    return result;
  }

  const Window root = DefaultRootWindow(display);
  const Atom net_client_list =
      ::XInternAtom(display, "_NET_CLIENT_LIST", False);
  const Atom net_wm_pid = ::XInternAtom(display, "_NET_WM_PID", False);
  const Atom net_wm_state = ::XInternAtom(display, "_NET_WM_STATE", False);
  const Atom skip_taskbar =
      ::XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
  const Atom net_wm_window_type =
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  const Atom exclude_types[] = {
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_MENU", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_COMBO", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_DND", False),
      ::XInternAtom(display, "_NET_WM_WINDOW_TYPE_SPLASH", False),
  };

  auto read_atom_list = [&](Window window, Atom property) -> std::vector<Atom> {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char *data = nullptr;
    std::vector<Atom> atoms;
    if (::XGetWindowProperty(display, window, property, 0, 1024, False, XA_ATOM,
                             &actual_type, &actual_format, &nitems,
                             &bytes_after, &data) != Success ||
        data == nullptr) {
      return atoms;
    }
    if (actual_format == 32 && nitems > 0) {
      const auto *values = reinterpret_cast<const Atom *>(data);
      atoms.assign(values, values + nitems);
    }
    ::XFree(data);
    return atoms;
  };

  Atom actual_type = None;
  int actual_format = 0;
  unsigned long nitems = 0;
  unsigned long bytes_after = 0;
  unsigned char *data = nullptr;
  if (::XGetWindowProperty(display, root, net_client_list, 0, 4096, False,
                           XA_WINDOW, &actual_type, &actual_format, &nitems,
                           &bytes_after, &data) != Success ||
      data == nullptr) {
    ::XCloseDisplay(display);
    return result;
  }
  if (actual_format != 32 || nitems == 0) {
    ::XFree(data);
    ::XCloseDisplay(display);
    return result;
  }

  const auto *windows = reinterpret_cast<const Window *>(data);
  for (unsigned long i = 0; i < nitems; ++i) {
    const Window window = windows[i];
    const std::vector<Atom> states = read_atom_list(window, net_wm_state);
    if (std::find(states.begin(), states.end(), skip_taskbar) != states.end()) {
      continue;
    }
    const std::vector<Atom> types = read_atom_list(window, net_wm_window_type);
    bool excluded = false;
    for (Atom type : types) {
      if (std::find(std::begin(exclude_types), std::end(exclude_types), type) !=
          std::end(exclude_types)) {
        excluded = true;
        break;
      }
    }
    if (excluded) {
      continue;
    }

    Atom pid_type = None;
    int pid_format = 0;
    unsigned long pid_nitems = 0;
    unsigned long pid_bytes_after = 0;
    unsigned char *pid_data = nullptr;
    if (::XGetWindowProperty(display, window, net_wm_pid, 0, 1, False,
                             XA_CARDINAL, &pid_type, &pid_format, &pid_nitems,
                             &pid_bytes_after, &pid_data) != Success ||
        pid_data == nullptr) {
      continue;
    }
    if (pid_format == 32 && pid_nitems > 0) {
      const auto pid = static_cast<pid_t>(
          *reinterpret_cast<const unsigned long *>(pid_data));
      if (pid > 0 &&
          std::find(result.begin(), result.end(), pid) == result.end()) {
        result.push_back(pid);
      }
    }
    ::XFree(pid_data);
  }

  ::XFree(data);
  ::XCloseDisplay(display);
#endif
  return result;
}

std::optional<uid_t> process_uid(pid_t pid) {
  const auto ids = ids_from_status(pid);
  if (ids.uid == static_cast<uid_t>(-1)) {
    return std::nullopt;
  }
  return ids.uid;
}

std::string username_of(uid_t uid) {
  if (passwd *pw = ::getpwuid(uid)) {
    if (pw->pw_name != nullptr && pw->pw_name[0] != '\0') {
      return pw->pw_name;
    }
  }
  return std::to_string(uid);
}

uid_t login_uid_min() {
  static const uid_t kMin = [] {
    std::ifstream input("/etc/login.defs");
    std::string line;
    while (std::getline(input, line)) {
      const auto hash = line.find('#');
      if (hash != std::string::npos) {
        line.resize(hash);
      }
      std::istringstream stream(line);
      std::string key;
      unsigned long value = 0;
      if (stream >> key >> value && key == "UID_MIN") {
        return static_cast<uid_t>(value);
      }
    }
    return static_cast<uid_t>(1000);
  }();
  return kMin;
}

bool is_login_user_uid(uid_t uid) {
  // uid_t(-1) = «невідомий» після невдалого читання /proc/…/status.
  if (uid == static_cast<uid_t>(-1)) {
    return false;
  }
  return uid >= login_uid_min();
}

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
      const auto parsed = std::from_chars(
          raw.data() + static_cast<std::ptrdiff_t>(pos),
          raw.data() + static_cast<std::ptrdiff_t>(raw.size()), value);
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

bool ProcessMemory::probe_read(std::string *error) const {
  const auto maps = regions();
  if (maps.empty()) {
    if (error) {
      *error = last_error().empty() ? "Немає доступної карти пам'яті процесу"
                                    : last_error();
    }
    return false;
  }
  std::byte byte{};
  bool saw_denied = false;
  for (const MemoryRegion &region : maps) {
    if (!region.readable || region.special_kernel()) {
      continue;
    }
    if (region.size() == 0) {
      continue;
    }
    const auto io = read(region.start, std::span<std::byte>(&byte, 1));
    if (io.ok()) {
      return true;
    }
    if (io.error == EPERM || io.error == EACCES) {
      saw_denied = true;
    }
  }
  if (error) {
    if (saw_denied) {
      *error = "Немає прав читати пам'ять цього процесу (Yama/ptrace). "
               "Без root спробуйте «Запустити» (гра як дочірній процес) "
               "або «З правами root» / sudo — для вже відкритої гри.";
    } else {
      *error = "Не вдалося прочитати жоден регіон пам'яті процесу";
    }
  }
  return false;
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
    const StatusIds ids = ids_from_status(pid);
    info.uid = ids.uid;
    info.tgid = ids.tgid > 0 ? ids.tgid : pid;
    if (info.uid != static_cast<uid_t>(-1)) {
      info.user = username_of(info.uid);
    }
    info.thread_count = process_thread_count(pid);
    info.command_line = command_line_of(pid);
    info.name = comm_of(pid);
    if (info.name.empty()) {
      info.name = info.command_line;
    }
    processes.push_back(std::move(info));
  }
  std::ranges::sort(processes,
                    [](const ProcessInfo &left, const ProcessInfo &right) {
                      if (left.user != right.user) {
                        return left.user < right.user;
                      }
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

bool elevated_privileges() { return ::geteuid() == 0; }

} // namespace gamex
