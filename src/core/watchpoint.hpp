#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace gamex {

inline constexpr int kMaxHardwareWatches = 4;

enum class WatchKind {
  Write,
  ReadWrite,
};

struct WatchHit {
  pid_t tid = -1;
  std::uintptr_t data_address = 0;
  std::uintptr_t instruction = 0;  // RIP після інструкції, що зробила запис
  std::vector<std::byte> bytes;    // байти біля RIP
  std::string disasm;              // Capstone або порожньо
  int slot = -1;                   // DR0..DR3
};

// Сесія ptrace: апаратні watchpoint на x86_64 (DR0..DR3).
// Лише для процесу, до якого дозволений ptrace (нащадок GameX).
class WatchSession {
public:
  WatchSession() = default;
  WatchSession(const WatchSession&) = delete;
  WatchSession& operator=(const WatchSession&) = delete;
  ~WatchSession();

  bool attached() const { return !tids_.empty(); }
  bool stopped() const { return stopped_; }
  pid_t pid() const { return pid_; }
  const WatchHit* last_hit() const { return last_hit_ ? &*last_hit_ : nullptr; }

  bool attach(pid_t pid, std::string* error);
  void detach();

  // size: 1, 2, 4 або 8; адреса має бути вирівняна.
  bool set_watch(std::uintptr_t address, std::size_t size, WatchKind kind, std::string* error);
  void clear_watches();

  bool resume(std::string* error);
  bool step(std::string* error);
  // Крок до ret (C3/C2/CB/CA) або ліміту кроків.
  bool run_until_ret(int max_steps, std::string* error);

  // Чекає SIGTRAP від watchpoint. cancel==true → вихід.
  std::optional<WatchHit> wait_hit(int timeout_ms, const std::atomic<bool>* cancel,
                                   std::string* error);

  static std::string disassemble(std::uintptr_t address, const std::byte* bytes, std::size_t size);

private:
  struct WatchSlot {
    bool used = false;
    std::uintptr_t address = 0;
    std::size_t size = 0;
    WatchKind kind = WatchKind::Write;
  };

  bool attach_tid(pid_t tid, std::string* error);
  bool apply_debug_regs(pid_t tid, std::string* error);
  bool read_regs(pid_t tid, std::uintptr_t* rip, std::string* error) const;
  bool read_insn_bytes(pid_t tid, std::uintptr_t rip, std::vector<std::byte>* out,
                       std::string* error) const;
  std::optional<WatchHit> capture_hit(pid_t tid, std::string* error);
  bool wait_stop(pid_t* tid_out, int* status_out, int timeout_ms,
                 const std::atomic<bool>* cancel, std::string* error);
  unsigned long build_dr7() const;
  static int len_code(std::size_t size);
  static int rw_code(WatchKind kind);
  static std::vector<pid_t> list_tids(pid_t pid);

  pid_t pid_ = -1;
  std::vector<pid_t> tids_;
  WatchSlot slots_[kMaxHardwareWatches]{};
  bool stopped_ = false;
  std::optional<WatchHit> last_hit_;
};

}  // namespace gamex
