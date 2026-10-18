#pragma once

#include "core/memory_region.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sys/types.h>

namespace gamex {

struct IoResult {
  std::size_t transferred = 0;
  int error = 0;

  bool ok() const { return error == 0 && transferred > 0; }
};

struct ProcessInfo {
  pid_t pid = -1;
  pid_t tgid = -1; // thread group id; для лідера процесу pid == tgid
  uid_t uid = static_cast<uid_t>(-1);
  int thread_count = 1; // /proc/<pid>/task
  std::string user;
  std::string name;
  std::string command_line;

  // На сучасних ядрах /proc/[pid] уже лише лідери; окремі TID не дублюються.
  bool is_process_leader() const { return pid > 0 && pid == tgid; }
};

class ProcessMemory {
public:
  ProcessMemory() = default;
  explicit ProcessMemory(pid_t pid);
  ProcessMemory(const ProcessMemory &) = delete;
  ProcessMemory &operator=(const ProcessMemory &) = delete;
  ProcessMemory(ProcessMemory &&other) noexcept;
  ProcessMemory &operator=(ProcessMemory &&other) noexcept;

  pid_t pid() const { return pid_; }
  bool valid() const;
  // True only while the same process identity (pid + starttime) still exists.
  bool alive() const;

  std::vector<MemoryRegion> regions() const;
  IoResult read(std::uintptr_t address, std::span<std::byte> buffer) const;
  IoResult write(std::uintptr_t address,
                 std::span<const std::byte> buffer) const;

  // Пробне читання 1 байта з першого доступного rw/r-регіону.
  // false + error при EPERM/EACCES або якщо жоден регіон не читається.
  bool probe_read(std::string *error) const;

  std::string last_error() const;

  static std::optional<ProcessMemory> open(pid_t pid, std::string *error);

private:
  pid_t pid_ = -1;
  // /proc/<pid>/stat starttime — захист від повторного використання PID.
  unsigned long long starttime_ = 0;
  mutable std::mutex mutex_;
  mutable std::string last_error_;
};

std::vector<ProcessInfo> list_processes();
std::string executable_path(pid_t pid);
std::string ptrace_scope();
std::optional<unsigned long long> process_starttime(pid_t pid);
// true якщо ефективний uid == 0 (типово після sudo).
bool elevated_privileges();
// Мінімальний UID звичайного логіну (/etc/login.defs UID_MIN, інакше 1000).
uid_t login_uid_min();
// Обліковий запис «реального» користувача (uid ≥ login_uid_min).
bool is_login_user_uid(uid_t uid);
std::optional<uid_t> process_uid(pid_t pid);
std::string username_of(uid_t uid);
int process_thread_count(pid_t pid);
// PID вікон з панелі оболонки (EWMH _NET_CLIENT_LIST без SKIP_TASKBAR).
// Порожньо без X11 / DISPLAY або на чистому Wayland без XWayland.
std::vector<pid_t> list_shell_panel_pids();

} // namespace gamex
