#include "core/watchpoint.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(GAMEX_HAS_CAPSTONE)
#include <capstone/capstone.h>
#endif

namespace gamex {
namespace {

std::string errno_message(const char *what) {
  return std::string(what) + ": " + std::strerror(errno);
}

bool is_ret_opcode(std::byte op) {
  const auto v = static_cast<unsigned>(op);
  return v == 0xc3 || v == 0xc2 || v == 0xcb || v == 0xca;
}

// g++ не дозволяє offsetof(user, u_debugreg[i]) зі змінним індексом.
std::size_t debugreg_user_offset(int index) {
  return offsetof(user, u_debugreg) +
         static_cast<std::size_t>(index) * sizeof(unsigned long);
}

} // namespace

WatchSession::~WatchSession() { detach(); }

std::vector<pid_t> WatchSession::list_tids(pid_t pid) {
  std::vector<pid_t> tids;
  const std::string path = "/proc/" + std::to_string(pid) + "/task";
  DIR *dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    return tids;
  }
  while (dirent *ent = ::readdir(dir)) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    char *end = nullptr;
    const long tid = std::strtol(ent->d_name, &end, 10);
    if (end != ent->d_name && *end == '\0' && tid > 0) {
      tids.push_back(static_cast<pid_t>(tid));
    }
  }
  ::closedir(dir);
  return tids;
}

int WatchSession::len_code(std::size_t size) {
  switch (size) {
  case 1:
    return 0;
  case 2:
    return 1;
  case 8:
    return 2;
  case 4:
  default:
    return 3;
  }
}

int WatchSession::rw_code(WatchKind kind) {
  return kind == WatchKind::ReadWrite ? 3 : 1; // 01 write, 11 R/W
}

unsigned long WatchSession::build_dr7() const {
  unsigned long dr7 = (1UL << 8) | (1UL << 9) | (1UL << 10); // LE, GE, reserved
  for (int i = 0; i < kMaxHardwareWatches; ++i) {
    if (!slots_[i].used) {
      continue;
    }
    dr7 |= 1UL << (i * 2); // local enable
    dr7 |= static_cast<unsigned long>(rw_code(slots_[i].kind)) << (16 + i * 4);
    dr7 |= static_cast<unsigned long>(len_code(slots_[i].size)) << (18 + i * 4);
  }
  return dr7;
}

bool WatchSession::attach_tid(pid_t tid, std::string *error) {
  if (::ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) != 0) {
    if (error) {
      *error = errno_message("ptrace(ATTACH)");
    }
    return false;
  }
  int status = 0;
  if (::waitpid(tid, &status, __WALL) < 0) {
    if (error) {
      *error = errno_message("waitpid після ATTACH");
    }
    ::ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
    return false;
  }
  if (!WIFSTOPPED(status)) {
    if (error) {
      *error = "Потік не зупинився після attach";
    }
    ::ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
    return false;
  }
  return true;
}

bool WatchSession::attach(pid_t pid, std::string *error) {
  detach();
  if (pid <= 0) {
    if (error) {
      *error = "Некоректний pid";
    }
    return false;
  }
  auto tids = list_tids(pid);
  if (tids.empty()) {
    tids.push_back(pid);
  }
  for (pid_t tid : tids) {
    if (!attach_tid(tid, error)) {
      detach();
      return false;
    }
    tids_.push_back(tid);
  }
  pid_ = pid;
  stopped_ = true;
  return true;
}

void WatchSession::detach() {
  clear_watches();
  for (pid_t tid : tids_) {
    ::ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
  }
  tids_.clear();
  pid_ = -1;
  stopped_ = false;
  last_hit_.reset();
}

bool WatchSession::apply_debug_regs(pid_t tid, std::string *error) {
  for (int i = 0; i < kMaxHardwareWatches; ++i) {
    const unsigned long addr = slots_[i].used ? slots_[i].address : 0;
    if (::ptrace(PTRACE_POKEUSER, tid,
                 reinterpret_cast<void *>(debugreg_user_offset(i)),
                 reinterpret_cast<void *>(addr)) != 0) {
      if (error) {
        *error = errno_message("ptrace(POKEUSER DR)");
      }
      return false;
    }
  }
  if (::ptrace(PTRACE_POKEUSER, tid,
               reinterpret_cast<void *>(debugreg_user_offset(6)),
               reinterpret_cast<void *>(0)) != 0) {
    if (error) {
      *error = errno_message("ptrace(POKEUSER DR6)");
    }
    return false;
  }
  const unsigned long dr7 = build_dr7();
  if (::ptrace(PTRACE_POKEUSER, tid,
               reinterpret_cast<void *>(debugreg_user_offset(7)),
               reinterpret_cast<void *>(dr7)) != 0) {
    if (error) {
      *error = errno_message("ptrace(POKEUSER DR7)");
    }
    return false;
  }
  return true;
}

bool WatchSession::set_watch(std::uintptr_t address, std::size_t size,
                             WatchKind kind, std::string *error) {
  if (!attached()) {
    if (error) {
      *error = "Спочатку attach";
    }
    return false;
  }
  if (size != 1 && size != 2 && size != 4 && size != 8) {
    if (error) {
      *error = "Розмір watchpoint: 1, 2, 4 або 8 байтів";
    }
    return false;
  }
  if (address % size != 0) {
    if (error) {
      *error = "Адреса має бути вирівняна під розмір watchpoint";
    }
    return false;
  }
  int free_slot = -1;
  for (int i = 0; i < kMaxHardwareWatches; ++i) {
    if (!slots_[i].used) {
      free_slot = i;
      break;
    }
  }
  if (free_slot < 0) {
    if (error) {
      *error = "Усі 4 апаратні watchpoint зайняті";
    }
    return false;
  }
  slots_[free_slot] = WatchSlot{true, address, size, kind};
  for (pid_t tid : tids_) {
    if (!apply_debug_regs(tid, error)) {
      slots_[free_slot].used = false;
      return false;
    }
  }
  return true;
}

void WatchSession::clear_watches() {
  for (auto &slot : slots_) {
    slot = {};
  }
  if (!attached()) {
    return;
  }
  for (pid_t tid : tids_) {
    apply_debug_regs(tid, nullptr);
  }
}

bool WatchSession::resume(std::string *error) {
  if (!attached()) {
    if (error) {
      *error = "Немає сесії ptrace";
    }
    return false;
  }
  for (pid_t tid : tids_) {
    // Скидаємо DR6, щоб не зациклитись на старому хіті.
    ::ptrace(PTRACE_POKEUSER, tid,
             reinterpret_cast<void *>(debugreg_user_offset(6)),
             reinterpret_cast<void *>(0));
    if (::ptrace(PTRACE_CONT, tid, nullptr, nullptr) != 0) {
      if (error) {
        *error = errno_message("ptrace(CONT)");
      }
      return false;
    }
  }
  stopped_ = false;
  return true;
}

bool WatchSession::read_regs(pid_t tid, std::uintptr_t *rip,
                             std::string *error) const {
  user_regs_struct regs{};
  if (::ptrace(PTRACE_GETREGS, tid, nullptr, &regs) != 0) {
    if (error) {
      *error = errno_message("ptrace(GETREGS)");
    }
    return false;
  }
  if (rip) {
    *rip = static_cast<std::uintptr_t>(regs.rip);
  }
  return true;
}

bool WatchSession::read_insn_bytes(pid_t tid, std::uintptr_t rip,
                                   std::vector<std::byte> *out,
                                   std::string *error) const {
  out->assign(16, std::byte{0});
  // Читаємо через process_vm_readv — працює і під ptrace.
  iovec local{reinterpret_cast<void *>(out->data()), out->size()};
  iovec remote{reinterpret_cast<void *>(rip), out->size()};
  const ssize_t n = ::process_vm_readv(pid_, &local, 1, &remote, 1, 0);
  if (n <= 0) {
    // Запасний шлях — PEEKDATA по словах.
    for (std::size_t i = 0; i < out->size(); i += sizeof(long)) {
      errno = 0;
      const long word = ::ptrace(PTRACE_PEEKDATA, tid,
                                 reinterpret_cast<void *>(rip + i), nullptr);
      if (errno != 0) {
        if (error) {
          *error = errno_message("ptrace(PEEKDATA)");
        }
        out->resize(i);
        return i > 0;
      }
      std::memcpy(out->data() + i, &word, sizeof(long));
    }
    return true;
  }
  out->resize(static_cast<std::size_t>(n));
  return true;
}

std::string WatchSession::disassemble(std::uintptr_t address,
                                      const std::byte *bytes,
                                      std::size_t size) {
#if defined(GAMEX_HAS_CAPSTONE)
  csh handle = 0;
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
    return {};
  }
  cs_insn *insn = nullptr;
  const std::size_t count =
      cs_disasm(handle, reinterpret_cast<const uint8_t *>(bytes), size, address,
                1, &insn);
  std::string text;
  if (count > 0 && insn != nullptr) {
    text = std::string(insn[0].mnemonic) + ' ' + insn[0].op_str;
    cs_free(insn, count);
  }
  cs_close(&handle);
  return text;
#else
  (void)address;
  (void)bytes;
  (void)size;
  return {};
#endif
}

std::optional<WatchHit> WatchSession::capture_hit(pid_t tid,
                                                  std::string *error) {
  errno = 0;
  const long dr6 =
      ::ptrace(PTRACE_PEEKUSER, tid,
               reinterpret_cast<void *>(debugreg_user_offset(6)), nullptr);
  if (errno != 0) {
    if (error) {
      *error = errno_message("ptrace(PEEKUSER DR6)");
    }
    return std::nullopt;
  }
  int slot = -1;
  for (int i = 0; i < kMaxHardwareWatches; ++i) {
    if ((dr6 & (1L << i)) != 0 && slots_[i].used) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    // Не наш debug trap (наприклад, single-step) — не вважаємо хітом watch.
    return std::nullopt;
  }
  WatchHit hit;
  hit.tid = tid;
  hit.slot = slot;
  hit.data_address = slots_[slot].address;
  if (!read_regs(tid, &hit.instruction, error)) {
    return std::nullopt;
  }
  if (!read_insn_bytes(tid, hit.instruction, &hit.bytes, error)) {
    return std::nullopt;
  }
  hit.disasm = disassemble(hit.instruction, hit.bytes.data(), hit.bytes.size());
  last_hit_ = hit;
  return hit;
}

bool WatchSession::wait_stop(pid_t *tid_out, int *status_out, int timeout_ms,
                             const std::atomic<bool> *cancel,
                             std::string *error) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      (timeout_ms < 0 ? std::chrono::steady_clock::duration::max()
                      : std::chrono::milliseconds(timeout_ms));
  for (;;) {
    if (cancel && cancel->load()) {
      if (error) {
        *error = "Очікування скасовано";
      }
      return false;
    }
    bool any_alive = false;
    for (pid_t tid : tids_) {
      int status = 0;
      const pid_t got = ::waitpid(tid, &status, __WALL | WNOHANG);
      if (got < 0) {
        if (errno == ECHILD) {
          continue;
        }
        if (error) {
          *error = errno_message("waitpid");
        }
        return false;
      }
      if (got == 0) {
        any_alive = true;
        continue;
      }
      any_alive = true;
      if (tid_out) {
        *tid_out = got;
      }
      if (status_out) {
        *status_out = status;
      }
      return true;
    }
    if (!any_alive) {
      if (error) {
        *error = "Процес завершився";
      }
      return false;
    }
    if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline) {
      if (error) {
        *error = "Час очікування вичерпано";
      }
      return false;
    }
    ::usleep(5'000);
  }
}

std::optional<WatchHit> WatchSession::wait_hit(int timeout_ms,
                                               const std::atomic<bool> *cancel,
                                               std::string *error) {
  if (!attached()) {
    if (error) {
      *error = "Немає сесії ptrace";
    }
    return std::nullopt;
  }
  if (stopped_) {
    if (!resume(error)) {
      return std::nullopt;
    }
  }
  for (;;) {
    pid_t tid = -1;
    int status = 0;
    if (!wait_stop(&tid, &status, timeout_ms, cancel, error)) {
      return std::nullopt;
    }
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      if (error) {
        *error = "Цільовий процес завершився";
      }
      return std::nullopt;
    }
    if (!WIFSTOPPED(status)) {
      continue;
    }
    stopped_ = true;
    const int sig = WSTOPSIG(status);
    if (sig != SIGTRAP && sig != SIGSTOP) {
      // Пересилаємо чужий сигнал і продовжуємо.
      ::ptrace(PTRACE_CONT, tid, nullptr,
               reinterpret_cast<void *>(static_cast<std::uintptr_t>(sig)));
      stopped_ = false;
      continue;
    }
    auto hit = capture_hit(tid, error);
    if (hit) {
      return hit;
    }
    // Single-step або чужий trap — продовжуємо, якщо ще чекаємо watch.
    if (sig == SIGTRAP) {
      ::ptrace(PTRACE_CONT, tid, nullptr, nullptr);
      stopped_ = false;
      continue;
    }
    // SIGSTOP без DR — лишаємось зупиненими.
    if (error) {
      *error = "Процес зупинено без спрацювання watchpoint";
    }
    return std::nullopt;
  }
}

bool WatchSession::step(std::string *error) {
  if (!attached() || !stopped_ || !last_hit_) {
    if (error) {
      *error = "Немає зупинки для кроку";
    }
    return false;
  }
  const pid_t tid = last_hit_->tid;
  if (::ptrace(PTRACE_SINGLESTEP, tid, nullptr, nullptr) != 0) {
    if (error) {
      *error = errno_message("ptrace(SINGLESTEP)");
    }
    return false;
  }
  stopped_ = false;
  pid_t stopped_tid = -1;
  int status = 0;
  if (!wait_stop(&stopped_tid, &status, 5000, nullptr, error)) {
    return false;
  }
  stopped_ = true;
  if (!WIFSTOPPED(status)) {
    if (error) {
      *error = "Крок не завершився зупинкою";
    }
    return false;
  }
  WatchHit hit = *last_hit_;
  hit.tid = stopped_tid;
  if (!read_regs(stopped_tid, &hit.instruction, error)) {
    return false;
  }
  if (!read_insn_bytes(stopped_tid, hit.instruction, &hit.bytes, error)) {
    return false;
  }
  hit.disasm = disassemble(hit.instruction, hit.bytes.data(), hit.bytes.size());
  hit.slot = -1;
  last_hit_ = hit;
  return true;
}

bool WatchSession::run_until_ret(int max_steps, std::string *error) {
  if (max_steps <= 0) {
    max_steps = 10'000;
  }
  for (int i = 0; i < max_steps; ++i) {
    if (!last_hit_ || last_hit_->bytes.empty()) {
      if (error) {
        *error = "Немає поточної інструкції";
      }
      return false;
    }
    if (is_ret_opcode(last_hit_->bytes.front())) {
      return true;
    }
    if (!step(error)) {
      return false;
    }
  }
  if (error) {
    *error = "Досягнуто ліміт кроків до ret";
  }
  return false;
}

} // namespace gamex
