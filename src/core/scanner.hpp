#pragma once

#include "core/process_memory.hpp"
#include "core/structure.hpp"
#include "core/value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gamex {

inline constexpr std::size_t kMaxScanHits = 2'000'000;

struct ScanQuery {
  ValueType type = ValueType::Int32;
  Endian endian = Endian::Little;
  CompareOp op = CompareOp::Equal;
  EncodedValue value;
  EncodedValue upper;
  std::vector<std::byte> mask;  // порожньо = усі байти точні; 0x00 = ??
  std::size_t alignment = 4;
  bool writable_only = true;
  std::uintptr_t region_start = 0;
  std::uintptr_t region_end = 0;
  std::uint64_t max_region_bytes = 0;
  std::string skip_path;

  bool limited_to_region() const { return region_end > region_start; }
};

struct ScanHit {
  std::uintptr_t address = 0;
  std::vector<std::byte> previous;
};

struct ScanStatus {
  bool ok = false;
  bool truncated = false;
  bool cancelled = false;
  bool snapshot = false;
  std::size_t hit_count = 0;
  std::uint64_t scanned = 0;
  std::uint64_t total = 0;
  std::string message;
};

// Повертає false, якщо пошук треба зупинити.
using ScanControl =
    std::function<bool(std::uint64_t scanned, std::uint64_t total)>;

class Scanner {
public:
  explicit Scanner(ProcessMemory &memory);

  ScanStatus scan(const ScanQuery &query, const ScanControl &control = {});
  ScanStatus refine(const ScanQuery &query, const ScanControl &control = {});
  // Один прохід: усі поля шаблону збігаються у своїх зсувах від бази.
  ScanStatus scan_structure(const StructureTemplate &tmpl, const ScanQuery &region,
                            const ScanControl &control = {});
  void reset();
  void replace_hits(std::vector<ScanHit> hits);

  std::size_t hit_count() const { return addresses_.size(); }
  ScanHit hit_at(std::size_t index) const;
  std::vector<ScanHit> hits() const;
  bool has_snapshot() const { return !snapshots_.empty(); }
  bool truncated() const { return truncated_; }

private:
  struct Snapshot {
    std::uintptr_t base = 0;
    std::vector<std::byte> data;
  };

  struct Slice {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
  };

  std::vector<Slice> slices_for(const ScanQuery &query, bool require_width,
                                std::size_t width) const;
  bool push_hit(std::uintptr_t address, const std::byte *data,
                std::size_t width);
  ScanStatus scan_values(const ScanQuery &query, const ScanControl &control);
  ScanStatus scan_unknown(const ScanQuery &query, const ScanControl &control);
  ScanStatus refine_hits(const ScanQuery &query, const ScanControl &control);
  ScanStatus refine_snapshot(const ScanQuery &query,
                             const ScanControl &control);

  ProcessMemory *memory_ = nullptr;
  std::vector<std::uintptr_t> addresses_;
  std::vector<std::byte> previous_;
  std::uint32_t width_ = 0;
  std::vector<Snapshot> snapshots_;
  bool truncated_ = false;
};

} // namespace gamex
