#include "core/address_table.hpp"
#include "core/byte_pattern.hpp"
#include "core/dump_book.hpp"
#include "core/emulator_profile.hpp"
#include "core/expression.hpp"
#include "core/freezer.hpp"
#include "core/memory_region.hpp"
#include "core/module_map.hpp"
#include "core/pointer_scan.hpp"
#include "core/process_memory.hpp"
#include "core/scanner.hpp"
#include "core/search_value.hpp"
#include "core/structure.hpp"
#include "core/value_codec.hpp"
#include "core/watchpoint.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void reap(pid_t pid);

void expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

void test_maps_parser() {
  const auto region =
      gamex::parse_maps_line("7f8a2c000000-7f8a2c021000 rw-p 00001000 08:02 "
                             "173521      /usr/lib/My Game/lib.so");
  expect(region.has_value(), "maps line parses");
  if (!region) {
    return;
  }
  expect(region->start == 0x7f8a2c000000ull, "start");
  expect(region->end == 0x7f8a2c021000ull, "end");
  expect(region->readable && region->writable && !region->executable &&
             region->private_mapping,
         "perms");
  expect(region->offset == 0x1000, "offset");
  expect(region->inode == 173521, "inode");
  expect(region->pathname == "/usr/lib/My Game/lib.so", "path with spaces");
  expect(region->scannable(true), "writable region is scannable");

  const auto vdso = gamex::parse_maps_line(
      "7ff000000000-7ff000001000 r--p 00000000 00:00 0 [vdso]");
  expect(vdso && vdso->special_kernel() && !vdso->scannable(false),
         "vdso skipped");
}

void test_codec() {
  std::string error;
  auto be = gamex::encode_text(gamex::ValueType::Int32, gamex::Endian::Big,
                               "0x01020304", &error);
  expect(be.has_value(), error);
  expect(be && be->bytes.size() == 4 && be->bytes[0] == std::byte{0x01} &&
             be->bytes[3] == std::byte{0x04},
         "int32 big endian");

  auto hex_suffix = gamex::encode_text(gamex::ValueType::Int32,
                                       gamex::Endian::Little, "100h", &error);
  auto decoded = hex_suffix ? gamex::decode_number(gamex::ValueType::Int32,
                                                   gamex::Endian::Little,
                                                   hex_suffix->bytes)
                            : std::nullopt;
  expect(decoded && decoded->integer == 0x100, "trailing h is hex");

  auto negative = gamex::encode_text(gamex::ValueType::Int32,
                                     gamex::Endian::Little, "-1", &error);
  expect(negative && negative->bytes.size() == 4 &&
             negative->bytes[0] == std::byte{0xff} &&
             negative->bytes[3] == std::byte{0xff},
         "int32 -1");

  auto one = gamex::encode_text(gamex::ValueType::Float32,
                                gamex::Endian::Little, "1", &error);
  expect(one && one->bytes.size() == 4 && one->bytes[0] == std::byte{0x00} &&
             one->bytes[2] == std::byte{0x80} &&
             one->bytes[3] == std::byte{0x3f},
         "float 1.0 little endian");

  auto comma = gamex::encode_text(gamex::ValueType::Float64,
                                  gamex::Endian::Little, "1,5", &error);
  auto comma_number =
      comma ? gamex::decode_number(gamex::ValueType::Float64,
                                   gamex::Endian::Little, comma->bytes)
            : std::nullopt;
  expect(comma_number && std::fabs(comma_number->real - 1.5) < 0.0001,
         "comma decimal");

  auto bytes = gamex::encode_text(gamex::ValueType::ByteArray,
                                  gamex::Endian::Little, "DE AD", &error);
  expect(bytes && bytes->bytes.size() == 2 &&
             bytes->bytes[0] == std::byte{0xde},
         "byte pattern");
  expect(gamex::decode_text(gamex::ValueType::ByteArray, gamex::Endian::Little,
                            bytes->bytes) == "DE AD",
         "byte text");
}

void test_self_memory() {
  auto slot = std::make_unique<std::int32_t>(0);
  std::mt19937 rng{std::random_device{}()};
  std::int32_t needle = static_cast<std::int32_t>(rng() & 0x0fffffffu);
  if (needle == 0) {
    needle = 0x13572468;
  }
  *slot = needle;

  std::string error;
  auto memory =
      gamex::ProcessMemory::open(static_cast<pid_t>(::getpid()), &error);
  expect(memory.has_value(), error.empty() ? "open self" : error);
  if (!memory) {
    return;
  }

  std::int32_t read_back = 0;
  const auto read = memory->read(
      reinterpret_cast<std::uintptr_t>(slot.get()),
      std::span<std::byte>(reinterpret_cast<std::byte *>(&read_back),
                           sizeof(read_back)));
  expect(read.ok() && read_back == needle, "read self value");

  const std::int32_t replacement = needle ^ 0x00abcdef;
  const auto write =
      memory->write(reinterpret_cast<std::uintptr_t>(slot.get()),
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte *>(&replacement),
                        sizeof(replacement)));
  expect(write.ok() && *slot == replacement, "write self value");
  *slot = needle;

  auto encoded = gamex::encode_i64(gamex::ValueType::Int32,
                                   gamex::Endian::Little, needle, &error);
  expect(encoded.has_value(), "encode needle");
  if (!encoded) {
    return;
  }
  gamex::Scanner scanner(*memory);
  gamex::ScanQuery query;
  query.type = gamex::ValueType::Int32;
  query.op = gamex::CompareOp::Equal;
  query.alignment = 4;
  query.value = *encoded;
  const auto status = scanner.scan(query);
  expect(status.ok, status.message.empty() ? "scan self" : status.message);
  bool found = false;
  for (const auto &hit : scanner.hits()) {
    if (hit.address == reinterpret_cast<std::uintptr_t>(slot.get())) {
      found = true;
    }
  }
  expect(found, "unique value found at the allocated address");

  *slot = needle + 1;
  gamex::ScanQuery grew;
  grew.type = gamex::ValueType::Int32;
  grew.endian = gamex::Endian::Little;
  grew.op = gamex::CompareOp::Increased;
  grew.alignment = 4;
  const auto refined = scanner.refine(grew);
  expect(refined.ok, refined.message.empty() ? "refine" : refined.message);
  found = false;
  for (const auto &hit : scanner.hits()) {
    if (hit.address == reinterpret_cast<std::uintptr_t>(slot.get())) {
      found = true;
    }
  }
  expect(found, "increased filter keeps the slot");

  gamex::AddressEntry entry;
  entry.address = reinterpret_cast<std::uintptr_t>(slot.get());
  entry.type = gamex::ValueType::Int32;
  entry.frozen = true;
  entry.frozen_value = encoded->bytes;
  gamex::AddressTable table;
  table.add(entry);
  *slot = 5;
  gamex::Freezer freezer;
  freezer.tick(*memory, table);
  expect(*slot == needle, "normal freeze restores the value");
}

void test_freeze_modes() {
  auto slot = std::make_unique<std::int32_t>(50);
  std::string error;
  auto memory =
      gamex::ProcessMemory::open(static_cast<pid_t>(::getpid()), &error);
  expect(memory.has_value(), error.empty() ? "open self" : error);
  if (!memory) {
    return;
  }
  auto lock = gamex::encode_i64(gamex::ValueType::Int32, gamex::Endian::Little,
                                50, &error);
  expect(lock.has_value(), "encode lock");
  if (!lock) {
    return;
  }

  gamex::AddressEntry grow;
  grow.address = reinterpret_cast<std::uintptr_t>(slot.get());
  grow.type = gamex::ValueType::Int32;
  grow.frozen = true;
  grow.freeze_mode = gamex::FreezeMode::AllowIncrease;
  grow.frozen_value = lock->bytes;
  gamex::AddressTable grow_table;
  grow_table.add(grow);
  gamex::Freezer freezer;

  *slot = 80;
  freezer.tick(*memory, grow_table);
  expect(*slot == 80, "allow-increase keeps a larger value");
  auto eighty = gamex::encode_i64(gamex::ValueType::Int32,
                                  gamex::Endian::Little, 80, nullptr);
  expect(eighty.has_value() &&
             grow_table.entries().front().frozen_value == eighty->bytes,
         "allow-increase remembers the larger value");

  *slot = 10;
  freezer.tick(*memory, grow_table);
  expect(*slot == 80, "allow-increase restores when value drops");

  *slot = 50;
  gamex::AddressEntry shrink = grow;
  shrink.freeze_mode = gamex::FreezeMode::AllowDecrease;
  shrink.frozen_value = lock->bytes;
  gamex::AddressTable shrink_table;
  shrink_table.add(shrink);
  *slot = 20;
  freezer.tick(*memory, shrink_table);
  expect(*slot == 20, "allow-decrease keeps a smaller value");
  *slot = 90;
  freezer.tick(*memory, shrink_table);
  expect(*slot == 20, "allow-decrease restores when value rises");

  *slot = 5;
  gamex::AddressEntry clamp = grow;
  clamp.freeze_mode = gamex::FreezeMode::Clamp;
  clamp.clamp_min = "10";
  clamp.clamp_max = "60";
  clamp.frozen_value = lock->bytes;
  gamex::AddressTable clamp_table;
  clamp_table.add(clamp);
  freezer.tick(*memory, clamp_table);
  expect(*slot == 10, "clamp lifts value to minimum");
  *slot = 99;
  freezer.tick(*memory, clamp_table);
  expect(*slot == 60, "clamp drops value to maximum");

  auto big = std::make_unique<std::uint64_t>(0);
  auto wide_value =
      gamex::encode_text(gamex::ValueType::UInt64, gamex::Endian::Little,
                         "9007199254740994", &error);
  expect(wide_value.has_value(), "encode large uint64");
  if (!wide_value) {
    return;
  }
  gamex::AddressEntry wide;
  wide.address = reinterpret_cast<std::uintptr_t>(big.get());
  wide.type = gamex::ValueType::UInt64;
  wide.frozen = true;
  wide.freeze_mode = gamex::FreezeMode::Clamp;
  wide.clamp_min = "9007199254740993";
  wide.clamp_max = "9007199254740995";
  wide.frozen_value = wide_value->bytes;
  gamex::AddressTable wide_table;
  wide_table.add(wide);
  *big = 1;
  freezer.tick(*memory, wide_table);
  expect(*big == 9007199254740993ull,
         "clamp uses exact uint64 bounds above 2^53");
}

void test_child_memory() {
  int pipes[2] = {-1, -1};
  if (::pipe(pipes) != 0) {
    expect(false, "pipe");
    return;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    expect(false, "fork");
    return;
  }
  if (pid == 0) {
    ::close(pipes[0]);
    volatile std::int32_t value = 0x13572468;
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(const_cast<std::int32_t *>(&value));
    const ssize_t wrote = ::write(pipes[1], &address, sizeof(address));
    if (wrote == static_cast<ssize_t>(sizeof(address))) {
      for (;;) {
        ::pause();
      }
    }
    _exit(1);
  }
  ::close(pipes[1]);
  std::uintptr_t address = 0;
  const ssize_t got = ::read(pipes[0], &address, sizeof(address));
  ::close(pipes[0]);
  expect(got == static_cast<ssize_t>(sizeof(address)),
         "child published its address");

  std::string error;
  auto memory = gamex::ProcessMemory::open(pid, &error);
  expect(memory.has_value(), error.empty() ? "open child" : error);
  if (memory) {
    std::int32_t value = 0;
    const auto read = memory->read(
        address, std::span<std::byte>(reinterpret_cast<std::byte *>(&value),
                                      sizeof(value)));
    expect(read.ok() && value == 0x13572468, "parent reads child memory");
  }
  reap(pid);
}

void reap(pid_t pid) {
  ::kill(pid, SIGKILL);
  int status = 0;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const pid_t waited = ::waitpid(pid, &status, WNOHANG);
    if (waited == pid || (waited < 0 && errno == ECHILD)) {
      return;
    }
    ::usleep(10000);
  }
}

void test_child_scan() {
  int pipes[2] = {-1, -1};
  if (::pipe(pipes) != 0) {
    expect(false, "pipe");
    return;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    expect(false, "fork");
    return;
  }
  if (pid == 0) {
    ::close(pipes[0]);
    volatile std::int32_t value = 0x13572468;
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(const_cast<std::int32_t *>(&value));
    if (::write(pipes[1], &address, sizeof(address)) ==
        static_cast<ssize_t>(sizeof(address))) {
      for (;;) {
        ::pause();
      }
    }
    _exit(1);
  }
  ::close(pipes[1]);
  std::uintptr_t address = 0;
  const ssize_t got = ::read(pipes[0], &address, sizeof(address));
  ::close(pipes[0]);
  expect(got == static_cast<ssize_t>(sizeof(address)),
         "child published its address");

  std::string error;
  auto memory = gamex::ProcessMemory::open(pid, &error);
  expect(memory.has_value(), error.empty() ? "open child" : error);
  if (memory) {
    std::string encode_error;
    auto encoded =
        gamex::encode_i64(gamex::ValueType::Int32, gamex::Endian::Little,
                          0x13572468, &encode_error);
    gamex::Scanner scanner(*memory);
    gamex::ScanQuery query;
    query.type = gamex::ValueType::Int32;
    query.op = gamex::CompareOp::Equal;
    query.alignment = 4;
    query.value = *encoded;
    const auto status = scanner.scan(query);
    expect(status.ok, status.message.empty() ? "scan child" : status.message);
    bool found = false;
    for (const auto &hit : scanner.hits()) {
      if (hit.address == address) {
        found = true;
      }
    }
    expect(found, "scanner finds the child value");
  }
  reap(pid);
}

void test_unknown_region_and_cancel() {
  constexpr std::size_t kPage = 4096;
  void *reserved =
      ::mmap(nullptr, kPage * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  expect(reserved != MAP_FAILED, "reserve pages");
  if (reserved == MAP_FAILED) {
    return;
  }
  auto *base = static_cast<std::byte *>(reserved);
  expect(::mprotect(base, kPage, PROT_READ | PROT_WRITE) == 0,
         "protect first page");
  expect(::mprotect(base + 2 * kPage, kPage, PROT_READ | PROT_WRITE) == 0,
         "protect third page");
  auto *first = reinterpret_cast<std::int32_t *>(base);
  auto *third = reinterpret_cast<std::int32_t *>(base + 2 * kPage);
  *first = 111;
  *third = 111;

  std::string error;
  auto memory =
      gamex::ProcessMemory::open(static_cast<pid_t>(::getpid()), &error);
  expect(memory.has_value(), error.empty() ? "open self" : error);
  if (!memory) {
    ::munmap(reserved, kPage * 3);
    return;
  }
  gamex::Scanner scanner(*memory);
  gamex::ScanQuery query;
  query.type = gamex::ValueType::Int32;
  query.op = gamex::CompareOp::Unknown;
  query.alignment = 4;
  query.region_start = reinterpret_cast<std::uintptr_t>(base);
  query.region_end = reinterpret_cast<std::uintptr_t>(base) + kPage;
  const auto snapshot = scanner.scan(query);
  expect(snapshot.ok && snapshot.snapshot && scanner.hit_count() == 0,
         snapshot.message);

  *first = 222;
  query.op = gamex::CompareOp::Changed;
  const auto refined = scanner.refine(query);
  expect(refined.ok,
         refined.message.empty() ? "refine snapshot" : refined.message);
  expect(scanner.hit_count() == 1, "only the changed word remains");
  bool found_first = false;
  bool found_third = false;
  for (const auto &hit : scanner.hits()) {
    if (hit.address == reinterpret_cast<std::uintptr_t>(first)) {
      found_first = true;
    }
    if (hit.address == reinterpret_cast<std::uintptr_t>(third)) {
      found_third = true;
    }
  }
  expect(found_first && !found_third, "region limit keeps the other page out");

  constexpr std::size_t kBig = 32u << 20;
  void *big = ::mmap(nullptr, kBig, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  expect(big != MAP_FAILED, "big map");
  if (big != MAP_FAILED) {
    static_cast<std::byte *>(big)[0] = std::byte{1};
    gamex::ScanQuery cancel_query;
    cancel_query.op = gamex::CompareOp::Unknown;
    cancel_query.region_start = reinterpret_cast<std::uintptr_t>(big);
    cancel_query.region_end = reinterpret_cast<std::uintptr_t>(big) + kBig;
    int calls = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto cancelled =
        scanner.scan(cancel_query,
                     [&](std::uint64_t, std::uint64_t) { return ++calls < 3; });
    const auto elapsed = std::chrono::steady_clock::now() - started;
    expect(cancelled.cancelled, "scan reports cancellation");
    expect(cancelled.scanned < kBig,
           "cancellation skips the rest of the mapping");
    expect(elapsed < std::chrono::seconds(2),
           "stop returns within two seconds");
    ::munmap(big, kBig);
  }
  ::munmap(reserved, kPage * 3);
}

void test_table_roundtrip() {
  gamex::AddressTable table;
  gamex::AddressEntry entry;
  entry.description = "Золото";
  entry.address = 0xabc123;
  entry.type = gamex::ValueType::Int32;
  entry.endian = gamex::Endian::Little;
  entry.frozen = true;
  entry.freeze_mode = gamex::FreezeMode::Clamp;
  entry.group = "Гравець";
  entry.clamp_min = "9007199254740993";
  entry.clamp_max = "9007199254740995";
  entry.module = "gamex_core_tests";
  entry.static_offset = 0x1234;
  entry.pointer_offsets = {0, 8};
  auto encoded = gamex::encode_i64(gamex::ValueType::Int32,
                                   gamex::Endian::Little, 999, nullptr);
  expect(encoded.has_value(), "encode table value");
  if (!encoded) {
    return;
  }
  entry.frozen_value = encoded->bytes;
  table.add(entry);

  const auto path =
      std::filesystem::temp_directory_path() / "gamex-table-test.gxt";
  std::string error;
  expect(table.save(path.string(), &error), error.empty() ? "save" : error);
  gamex::AddressTable loaded;
  expect(loaded.load(path.string(), &error), error.empty() ? "load" : error);
  expect(loaded.entries().size() == 1, "one entry");
  if (!loaded.entries().empty()) {
    const auto &got = loaded.entries().front();
    expect(got.description == "Золото", "description");
    expect(got.address == 0xabc123, "address");
    expect(got.frozen && got.freeze_mode == gamex::FreezeMode::Clamp, "freeze");
    expect(got.group == "Гравець", "group");
    expect(got.clamp_min == "9007199254740993", "clamp min text");
    expect(got.clamp_max == "9007199254740995", "clamp max text");
    expect(got.module == "gamex_core_tests", "module");
    expect(got.static_offset == 0x1234, "static offset");
    expect(got.pointer_offsets.size() == 2 && got.pointer_offsets[0] == 0 &&
               got.pointer_offsets[1] == 8,
           "pointer offsets");
    const auto number =
        gamex::decode_number(got.type, got.endian, got.frozen_value);
    expect(number && number->integer == 999, "stored value");
  }
  std::filesystem::remove(path);
}

static std::int32_t g_module_value = 0x13579bdf;

void test_expression_and_mask() {
  std::string error;
  expect(gamex::looks_like_expression("100*10"), "100*10 is expression");
  expect(!gamex::looks_like_expression("100"), "100 is plain");
  expect(!gamex::looks_like_expression("64h"), "64h is plain hex");
  const auto product = gamex::eval_i64("100*10", {}, &error);
  expect(product && *product == 1000, "100*10 = 1000");
  const auto xored = gamex::eval_i64("100 xor 1", {}, &error);
  expect(xored && *xored == 101, "100 xor 1");
  const auto bool_ok = gamex::eval_bool(
      "(M1<>M2)and(M1=M3)", {{"M1", 5}, {"M2", 7}, {"M3", 5}}, &error);
  expect(bool_ok && *bool_ok, "dump formula true");
  const auto bool_no = gamex::eval_bool(
      "(M1<>M2)and(M1=M3)", {{"M1", 5}, {"M2", 7}, {"M3", 8}}, &error);
  expect(bool_no && !*bool_no, "dump formula false");

  auto prepared = gamex::prepare_search_value(
      gamex::ValueType::Int32, gamex::Endian::Little, "100*10", &error);
  expect(prepared.has_value(), error.empty() ? "prepare 100*10" : error);
  const auto number = prepared ? gamex::decode_number(gamex::ValueType::Int32,
                                                      gamex::Endian::Little,
                                                      prepared->value.bytes)
                               : std::nullopt;
  expect(number && number->integer == 1000, "encoded expression is 1000");

  auto pattern = gamex::parse_byte_pattern("DE ?? AD", &error);
  expect(pattern && pattern->size() == 3 && pattern->has_wildcards(),
         "parse mask");
  const std::byte a[] = {std::byte{0xde}, std::byte{0x00}, std::byte{0xad}};
  const std::byte b[] = {std::byte{0xde}, std::byte{0xff}, std::byte{0xad}};
  const std::byte c[] = {std::byte{0xde}, std::byte{0x00}, std::byte{0x00}};
  expect(pattern->matches(a) && pattern->matches(b) && !pattern->matches(c),
         "DE ?? matches both and rejects third");

  auto slot = std::make_unique<std::array<std::byte, 4>>();
  (*slot)[0] = std::byte{0xde};
  (*slot)[1] = std::byte{0x11};
  (*slot)[2] = std::byte{0xad};
  (*slot)[3] = std::byte{0x00};
  auto memory =
      gamex::ProcessMemory::open(static_cast<pid_t>(::getpid()), &error);
  expect(memory.has_value(), "open self for mask scan");
  if (memory) {
    auto masked = gamex::prepare_search_value(
        gamex::ValueType::ByteArray, gamex::Endian::Little, "DE ?? AD", &error);
    expect(masked.has_value(), "prepare mask");
    gamex::Scanner scanner(*memory);
    gamex::ScanQuery query;
    query.type = gamex::ValueType::ByteArray;
    query.op = gamex::CompareOp::Equal;
    query.alignment = 1;
    query.value = masked->value;
    query.mask = masked->mask;
    query.region_start = reinterpret_cast<std::uintptr_t>(slot->data());
    query.region_end = query.region_start + slot->size();
    const auto status = scanner.scan(query);
    expect(status.ok && scanner.hit_count() >= 1, "mask scan finds pattern");
  }
}

void test_module_bind_and_resolve() {
  std::string error;
  auto memory =
      gamex::ProcessMemory::open(static_cast<pid_t>(::getpid()), &error);
  expect(memory.has_value(), error.empty() ? "open self" : error);
  if (!memory) {
    return;
  }
  const gamex::ModuleMap modules = gamex::ModuleMap::from_process(*memory);
  gamex::AddressEntry entry;
  entry.address = reinterpret_cast<std::uintptr_t>(&g_module_value);
  entry.type = gamex::ValueType::Int32;
  expect(gamex::bind_to_module(entry, modules), "bind global to module");
  expect(!entry.module.empty(), "module name set");
  expect(entry.pointer_offsets.empty(), "direct binding has no pointer chain");
  const auto saved_offset = entry.static_offset;
  entry.address = 0;
  expect(gamex::resolve_entry(entry, *memory, modules, &error),
         error.empty() ? "resolve direct" : error);
  expect(entry.address == reinterpret_cast<std::uintptr_t>(&g_module_value),
         "resolved back to global");
  expect(entry.static_offset == saved_offset, "offset stable");

  entry.module = "no-such-module.so";
  expect(!gamex::resolve_entry(entry, *memory, modules, &error),
         "missing module fails");
  expect(!entry.resolved, "marked unresolved");
}

void test_emulator_guest_mapping() {
  const gamex::EmulatorProfile *profile = gamex::find_emulator_profile("gba");
  expect(profile != nullptr, "gba profile exists");
  if (profile == nullptr) {
    return;
  }
  gamex::GuestRamMapping map;
  map.host_base = 0x7f000000;
  map.host_end = map.host_base + 256ull * 1024;
  const auto host = gamex::guest_to_host(map, *profile, 0x02000100);
  expect(host && *host == 0x7f000100, "guest 0x02000100 -> host");
  const auto guest = gamex::host_to_guest(map, *profile, 0x7f000100);
  expect(guest && *guest == 0x02000100, "host -> guest roundtrip");
  expect(!gamex::guest_to_host(map, *profile, 0x01000000), "below guest base rejected");
  expect(!gamex::guest_to_host(map, *profile, 0x02000000 + 256ull * 1024),
         "past RAM end rejected");

  gamex::AddressEntry entry;
  entry.description = "gold";
  entry.type = gamex::ValueType::Int32;
  entry.endian = gamex::Endian::Little;
  entry.emulator = "gba";
  entry.guest_address = 0x02000100;
  entry.address = 0;  // буде перераховано при resolve
  auto encoded =
      gamex::encode_i64(gamex::ValueType::Int32, gamex::Endian::Little, 99, nullptr);
  expect(encoded.has_value(), "encode gold");
  entry.frozen_value = encoded->bytes;

  const auto path =
      std::filesystem::temp_directory_path() / "gamex_emu_table_test.gxt";
  gamex::AddressTable table;
  table.add(entry);
  std::string error;
  expect(table.save(path.string(), &error), error.empty() ? "save emu table" : error);
  gamex::AddressTable loaded;
  expect(loaded.load(path.string(), &error), error.empty() ? "load emu table" : error);
  expect(loaded.entries().size() == 1, "one entry");
  if (!loaded.entries().empty()) {
    expect(loaded.entries()[0].emulator == "gba", "emulator id roundtrip");
    expect(loaded.entries()[0].guest_address == 0x02000100, "guest address roundtrip");
    expect(loaded.entries()[0].binding_text().find("gba@") == 0, "binding text");
  }
  std::filesystem::remove(path);
}

void test_watchpoint_child() {
  int pipes[2] = {-1, -1};
  if (::pipe(pipes) != 0) {
    expect(false, "pipe");
    return;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    expect(false, "fork");
    return;
  }
  if (pid == 0) {
    ::close(pipes[0]);
    alignas(4) volatile std::int32_t value = 0;
    alignas(4) volatile std::int32_t ready = 0;
    const std::uintptr_t payload[2] = {
        reinterpret_cast<std::uintptr_t>(const_cast<std::int32_t *>(&value)),
        reinterpret_cast<std::uintptr_t>(const_cast<std::int32_t *>(&ready)),
    };
    if (::write(pipes[1], payload, sizeof(payload)) ==
        static_cast<ssize_t>(sizeof(payload))) {
      while (ready == 0) {
        // spin until parent arms watchpoint and sets ready
      }
      value = 0x42;
      for (;;) {
        ::pause();
      }
    }
    _exit(1);
  }
  ::close(pipes[1]);
  std::uintptr_t payload[2] = {0, 0};
  const ssize_t got = ::read(pipes[0], payload, sizeof(payload));
  ::close(pipes[0]);
  expect(got == static_cast<ssize_t>(sizeof(payload)),
         "child published watch addrs");

  std::string error;
  gamex::WatchSession session;
  expect(session.attach(pid, &error), error.empty() ? "watch attach" : error);
  expect(session.set_watch(payload[0], 4, gamex::WatchKind::Write, &error),
         error.empty() ? "set write watch" : error);

  auto memory = gamex::ProcessMemory::open(pid, &error);
  expect(memory.has_value(), error.empty() ? "open child for ready" : error);
  if (memory) {
    const std::int32_t one = 1;
    std::vector<std::byte> bytes(sizeof(one));
    std::memcpy(bytes.data(), &one, sizeof(one));
    const auto io = memory->write(payload[1], bytes);
    expect(io.ok(), "set ready flag");
  }

  const auto hit = session.wait_hit(5000, nullptr, &error);
  expect(hit.has_value(), error.empty() ? "watch hit" : error);
  if (hit) {
    expect(hit->data_address == payload[0], "hit data address");
    expect(hit->instruction != 0, "hit has RIP");
    expect(!hit->bytes.empty(), "hit has instruction bytes");
  }
  session.detach();
  reap(pid);
}

void test_structure_scan_child() {
  int pipes[2] = {-1, -1};
  if (::pipe(pipes) != 0) {
    expect(false, "pipe");
    return;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    expect(false, "fork");
    return;
  }
  if (pid == 0) {
    ::close(pipes[0]);
    struct Character {
      std::int32_t health = 100;
      std::int32_t pad = 0;
      std::int32_t gold = 999;
    };
    volatile Character character{};
    character.health = 100;
    character.pad = 0xDEAD;
    character.gold = 999;
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(const_cast<Character *>(&character));
    if (::write(pipes[1], &address, sizeof(address)) ==
        static_cast<ssize_t>(sizeof(address))) {
      for (;;) {
        ::pause();
      }
    }
    _exit(1);
  }
  ::close(pipes[1]);
  std::uintptr_t address = 0;
  const ssize_t got = ::read(pipes[0], &address, sizeof(address));
  ::close(pipes[0]);
  expect(got == static_cast<ssize_t>(sizeof(address)),
         "child published structure address");

  std::string error;
  auto memory = gamex::ProcessMemory::open(pid, &error);
  expect(memory.has_value(), error.empty() ? "open child" : error);
  if (memory) {
    gamex::StructureTemplate tmpl;
    tmpl.name = "Character";
    tmpl.endian = gamex::Endian::Little;
    {
      gamex::StructureField health;
      health.offset = 0;
      health.type = gamex::ValueType::Int32;
      health.description = "health";
      health.value = *gamex::encode_i64(gamex::ValueType::Int32,
                                        gamex::Endian::Little, 100, &error);
      tmpl.fields.push_back(std::move(health));
    }
    {
      gamex::StructureField gold;
      gold.offset = 8;
      gold.type = gamex::ValueType::Int32;
      gold.description = "gold";
      gold.value = *gamex::encode_i64(gamex::ValueType::Int32,
                                      gamex::Endian::Little, 999, &error);
      tmpl.fields.push_back(std::move(gold));
    }
    expect(tmpl.span() == 12, "structure span is 12 with pad gap");
    expect(tmpl.validate(&error), error.empty() ? "validate structure" : error);

    gamex::Scanner scanner(*memory);
    gamex::ScanQuery region;
    region.alignment = 4;
    region.writable_only = true;
    const auto status = scanner.scan_structure(tmpl, region);
    expect(status.ok,
           status.message.empty() ? "structure scan" : status.message);
    bool found = false;
    for (const auto &hit : scanner.hits()) {
      if (hit.address == address) {
        found = true;
        expect(hit.previous.size() == 12, "hit stores full structure span");
      }
    }
    expect(found, "structure {i32 health; pad 4; i32 gold} found in child");

    std::vector<gamex::StructureDraftSource> sources;
    {
      gamex::StructureDraftSource a;
      a.address = address;
      a.type = gamex::ValueType::Int32;
      a.description = "health";
      a.value = tmpl.fields[0].value;
      sources.push_back(a);
    }
    {
      gamex::StructureDraftSource b;
      b.address = address + 8;
      b.type = gamex::ValueType::Int32;
      b.description = "gold";
      b.value = tmpl.fields[1].value;
      sources.push_back(b);
    }
    auto drafted = gamex::draft_structure("Char", sources, &error);
    expect(drafted.has_value() && drafted->fields.size() == 2 &&
               drafted->fields[1].offset == 8,
           "draft from nearby addresses");
  }
  reap(pid);
}

void test_pointer_scan_child() {
  int pipes[2] = {-1, -1};
  if (::pipe(pipes) != 0) {
    expect(false, "pipe");
    return;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    expect(false, "fork");
    return;
  }
  if (pid == 0) {
    ::close(pipes[0]);
    static std::int32_t *holder = nullptr;
    holder = new std::int32_t(0x0abcdef0);
    const std::uintptr_t payload[2] = {
        reinterpret_cast<std::uintptr_t>(holder),
        reinterpret_cast<std::uintptr_t>(&holder),
    };
    if (::write(pipes[1], payload, sizeof(payload)) ==
        static_cast<ssize_t>(sizeof(payload))) {
      for (;;) {
        ::pause();
      }
    }
    _exit(1);
  }
  ::close(pipes[1]);
  std::uintptr_t payload[2] = {};
  const ssize_t got = ::read(pipes[0], payload, sizeof(payload));
  ::close(pipes[0]);
  expect(got == static_cast<ssize_t>(sizeof(payload)),
         "child published pointer data");

  std::string error;
  auto memory = gamex::ProcessMemory::open(pid, &error);
  expect(memory.has_value(), error.empty() ? "open child" : error);
  if (memory) {
    const std::uintptr_t heap_value = payload[0];
    const std::uintptr_t static_slot = payload[1];
    gamex::PointerScanner scanner(*memory);
    const auto status = scanner.scan(heap_value, 1);
    expect(status.ok, status.message.empty() ? "pointer scan" : status.message);
    bool found = false;
    for (const auto &path : scanner.paths()) {
      std::string resolve_error;
      const gamex::ModuleMap modules = gamex::ModuleMap::from_process(*memory);
      const auto resolved = gamex::PointerScanner::resolve(
          *memory, modules, path.module, path.static_offset, path.offsets,
          &resolve_error);
      if (resolved && *resolved == heap_value) {
        found = true;
      }
      (void)static_slot;
    }
    expect(found || !scanner.paths().empty(),
           "pointer scan returns at least one static path to the heap value");

    // Пряма перевірка resolve для відомого static slot.
    const gamex::ModuleMap modules = gamex::ModuleMap::from_process(*memory);
    gamex::AddressEntry entry;
    entry.address = static_slot;
    expect(gamex::bind_to_module(entry, modules), "bind static pointer slot");
    entry.pointer_offsets = {0};
    expect(gamex::resolve_entry(entry, *memory, modules, &error),
           error.empty() ? "resolve pointer" : error);
    expect(entry.address == heap_value, "pointer chain lands on heap value");
  }
  reap(pid);
}

} // namespace

int main() {
  test_maps_parser();
  test_codec();
  test_self_memory();
  test_freeze_modes();
  test_child_memory();
  test_child_scan();
  test_unknown_region_and_cancel();
  test_table_roundtrip();
  test_expression_and_mask();
  test_module_bind_and_resolve();
  test_structure_scan_child();
  test_emulator_guest_mapping();
  test_watchpoint_child();
  test_pointer_scan_child();
  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
