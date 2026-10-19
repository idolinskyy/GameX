#include "core/scanner.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include <errno.h>

namespace gamex {
namespace {

std::size_t value_width(const ScanQuery &query) {
  if (query.op == CompareOp::Unknown) {
    return 0;
  }
  if (is_fixed_width(query.type)) {
    return fixed_width(query.type);
  }
  return query.value.bytes.size();
}

std::uintptr_t align_up(std::uintptr_t address, std::size_t alignment) {
  if (alignment <= 1) {
    return address;
  }
  const std::uintptr_t remainder = address % alignment;
  if (remainder == 0) {
    return address;
  }
  return address + (alignment - remainder);
}

bool bytes_equal(const std::byte *data,
                 const std::vector<std::byte> &expected) {
  return expected.size() > 0 &&
         std::memcmp(data, expected.data(), expected.size()) == 0;
}

bool numeric_relation(CompareOp op, const Number &current, const Number &bound,
                      const Number *upper) {
  const int order = compare_numbers(current, bound);
  switch (op) {
  case CompareOp::Greater:
    return order > 0;
  case CompareOp::GreaterOrEqual:
    return order >= 0;
  case CompareOp::Less:
    return order < 0;
  case CompareOp::LessOrEqual:
    return order <= 0;
  case CompareOp::Between:
    return upper != nullptr && order >= 0 &&
           compare_numbers(current, *upper) <= 0;
  default:
    return false;
  }
}

bool masked_equal(const std::byte *data, const std::vector<std::byte> &bytes,
                  const std::vector<std::byte> &mask) {
  if (bytes.size() != mask.size()) {
    return bytes_equal(data, bytes);
  }
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (mask[i] == std::byte{0x00}) {
      continue;
    }
    if (data[i] != bytes[i]) {
      return false;
    }
  }
  return true;
}

bool matches_current(const ScanQuery &query, const std::byte *data,
                     std::size_t width) {
  const std::span<const std::byte> current(data, width);
  switch (query.op) {
  case CompareOp::Equal:
    if (query.value.bytes.size() != width) {
      return false;
    }
    if (!query.mask.empty()) {
      return masked_equal(data, query.value.bytes, query.mask);
    }
    return bytes_equal(data, query.value.bytes);
  case CompareOp::NotEqual:
    if (query.value.bytes.size() != width) {
      return false;
    }
    if (!query.mask.empty()) {
      return !masked_equal(data, query.value.bytes, query.mask);
    }
    return !bytes_equal(data, query.value.bytes);
  case CompareOp::Greater:
  case CompareOp::GreaterOrEqual:
  case CompareOp::Less:
  case CompareOp::LessOrEqual:
  case CompareOp::Between: {
    const auto got = decode_number(query.type, query.endian, current);
    const auto bound =
        decode_number(query.type, query.endian, query.value.bytes);
    if (!got || !bound) {
      return false;
    }
    std::optional<Number> upper;
    if (query.op == CompareOp::Between) {
      upper = decode_number(query.type, query.endian, query.upper.bytes);
      if (!upper) {
        return false;
      }
    }
    return numeric_relation(query.op, *got, *bound, upper ? &*upper : nullptr);
  }
  default:
    return false;
  }
}

bool matches_previous(const ScanQuery &query, const std::byte *previous,
                      const std::byte *data, std::size_t width) {
  switch (query.op) {
  case CompareOp::Changed:
    return std::memcmp(previous, data, width) != 0;
  case CompareOp::Unchanged:
    return std::memcmp(previous, data, width) == 0;
  case CompareOp::Increased:
  case CompareOp::Decreased: {
    const auto before = decode_number(
        query.type, query.endian, std::span<const std::byte>(previous, width));
    const auto now = decode_number(query.type, query.endian,
                                   std::span<const std::byte>(data, width));
    if (!before || !now) {
      return false;
    }
    const int order = compare_numbers(*now, *before);
    return query.op == CompareOp::Increased ? order > 0 : order < 0;
  }
  default:
    return matches_current(query, data, width);
  }
}

bool relative_op(CompareOp op) {
  return op == CompareOp::Increased || op == CompareOp::Decreased ||
         op == CompareOp::Changed || op == CompareOp::Unchanged;
}

bool keep_going(const ScanControl &control, std::uint64_t scanned,
                std::uint64_t total) {
  return !control || control(scanned, total);
}

} // namespace

Scanner::Scanner(ProcessMemory &memory) : memory_(&memory) {}

void Scanner::reset() {
  addresses_.clear();
  previous_.clear();
  width_ = 0;
  snapshots_.clear();
  truncated_ = false;
}

void Scanner::replace_hits(std::vector<ScanHit> hits) {
  addresses_.clear();
  previous_.clear();
  width_ = 0;
  truncated_ = false;
  snapshots_.clear();
  for (ScanHit &hit : hits) {
    if (hit.previous.empty()) {
      continue;
    }
    if (width_ == 0) {
      width_ = static_cast<std::uint32_t>(hit.previous.size());
    }
    if (hit.previous.size() != width_) {
      continue;
    }
    addresses_.push_back(hit.address);
    previous_.insert(previous_.end(), hit.previous.begin(), hit.previous.end());
  }
}

ScanHit Scanner::hit_at(std::size_t index) const {
  ScanHit hit;
  if (index >= addresses_.size() || width_ == 0) {
    return hit;
  }
  hit.address = addresses_[index];
  const auto *begin =
      previous_.data() + static_cast<std::size_t>(index) * width_;
  hit.previous.assign(begin, begin + width_);
  return hit;
}

std::vector<ScanHit> Scanner::hits() const {
  std::vector<ScanHit> copy;
  copy.reserve(addresses_.size());
  for (std::size_t index = 0; index < addresses_.size(); ++index) {
    copy.push_back(hit_at(index));
  }
  return copy;
}

bool Scanner::push_hit(std::uintptr_t address, const std::byte *data,
                       std::size_t width) {
  if (addresses_.size() >= kMaxScanHits) {
    truncated_ = true;
    return false;
  }
  if (width_ == 0) {
    width_ = static_cast<std::uint32_t>(width);
  }
  addresses_.push_back(address);
  previous_.insert(previous_.end(), data, data + width);
  return true;
}

std::vector<Scanner::Slice> Scanner::slices_for(const ScanQuery &query,
                                                bool require_width,
                                                std::size_t width) const {
  std::vector<Slice> slices;
  if (memory_ == nullptr) {
    return slices;
  }
  for (const MemoryRegion &region : memory_->regions()) {
    if (!region.scannable(query.writable_only)) {
      continue;
    }
    if (!query.skip_path.empty() && region.pathname == query.skip_path) {
      continue;
    }
    if (!query.limited_to_region() && query.max_region_bytes != 0 &&
        region.size() > query.max_region_bytes) {
      continue;
    }
    std::uintptr_t start = region.start;
    std::uintptr_t end = region.end;
    if (query.limited_to_region()) {
      if (end <= query.region_start || start >= query.region_end) {
        continue;
      }
      start = std::max(start, query.region_start);
      end = std::min(end, query.region_end);
    }
    if (end <= start || (require_width && end - start < width)) {
      continue;
    }
    slices.push_back(Slice{start, end});
  }
  return slices;
}

ScanStatus Scanner::scan(const ScanQuery &query, const ScanControl &control) {
  reset();
  if (memory_ == nullptr || !memory_->alive()) {
    ScanStatus status;
    status.message = "Процес недоступний";
    return status;
  }
  if (query.op == CompareOp::Unknown) {
    return scan_unknown(query, control);
  }
  if (relative_op(query.op) || query.op == CompareOp::NotEqual) {
    ScanStatus status;
    status.message = "Ця умова працює під час відсіву. Перший пошук приймає "
                     "рівність, порівняння, діапазон "
                     "або невідоме значення.";
    return status;
  }
  const std::size_t width = value_width(query);
  if (width == 0 || query.alignment == 0) {
    ScanStatus status;
    status.message = "Порожнє значення або нульове вирівнювання";
    return status;
  }
  if (query.op != CompareOp::Equal && !is_fixed_width(query.type)) {
    ScanStatus status;
    status.message =
        "Для рядка і байтів перший пошук підтримує лише точну рівність";
    return status;
  }
  if (query.value.bytes.size() != width) {
    ScanStatus status;
    status.message = "Значення не зібране";
    return status;
  }
  if (query.op == CompareOp::Between && query.upper.bytes.size() != width) {
    ScanStatus status;
    status.message = "Верхня межа діапазону не зібрана";
    return status;
  }
  return scan_values(query, control);
}

ScanStatus Scanner::scan_unknown(const ScanQuery &query,
                                 const ScanControl &control) {
  ScanStatus status;
  const auto slices = slices_for(query, false, 0);
  for (const Slice &slice : slices) {
    status.total += static_cast<std::uint64_t>(slice.end - slice.start);
  }
  bool denied = false;
  bool any_read = false;
  constexpr std::size_t kChunk = 1u << 20;
  std::vector<std::byte> window(kChunk);

  for (const Slice &slice : slices) {
    Snapshot snapshot;
    snapshot.base = slice.start;
    snapshot.data.reserve(static_cast<std::size_t>(slice.end - slice.start));
    std::uintptr_t cursor = slice.start;
    bool region_denied = false;
    while (cursor < slice.end) {
      if (!keep_going(control, status.scanned, status.total)) {
        status.cancelled = true;
        break;
      }
      const std::size_t want = std::min<std::size_t>(
          kChunk, static_cast<std::size_t>(slice.end - cursor));
      window.resize(want);
      const auto io = memory_->read(cursor, window);
      if (io.error == EPERM || io.error == EACCES) {
        denied = true;
        region_denied = true;
        break;
      }
      if (io.transferred == 0) {
        break;
      }
      any_read = true;
      snapshot.data.insert(snapshot.data.end(), window.begin(),
                           window.begin() +
                               static_cast<std::ptrdiff_t>(io.transferred));
      cursor += io.transferred;
      status.scanned += io.transferred;
      if (io.transferred < want) {
        break;
      }
    }
    if (!snapshot.data.empty()) {
      snapshots_.push_back(std::move(snapshot));
    }
    if (status.cancelled || region_denied) {
      break;
    }
  }

  if (!any_read && denied) {
    snapshots_.clear();
    status.message =
        "Немає прав читати пам'ять цього процесу. Ядро Yama "
        "дозволяє ptrace лише нащадкам, "
        "тому гру варто запускати з GameX, а не підключатись до вже "
        "відкритого процесу.";
    return status;
  }
  status.ok = true;
  status.snapshot = !snapshots_.empty();
  status.scanned = 0;
  for (const Snapshot &snapshot : snapshots_) {
    status.scanned += snapshot.data.size();
  }
  if (status.cancelled) {
    status.message = "Знімок зупинено";
  } else if (status.snapshot) {
    status.message = "Знімок готовий";
  } else {
    status.message = "Немає регіонів для знімка";
    status.ok = false;
  }
  return status;
}

ScanStatus Scanner::scan_values(const ScanQuery &query,
                                const ScanControl &control) {
  ScanStatus status;
  const std::size_t width = query.value.bytes.size();
  width_ = static_cast<std::uint32_t>(width);
  const auto slices = slices_for(query, true, width);
  for (const Slice &slice : slices) {
    status.total += static_cast<std::uint64_t>(slice.end - slice.start);
  }

  bool denied = false;
  bool any_read = false;
  constexpr std::size_t kChunk = 1u << 20;
  std::vector<std::byte> window(kChunk + width);

  for (const Slice &slice : slices) {
    std::size_t carry = 0;
    std::uintptr_t cursor = slice.start;
    while (cursor < slice.end && !truncated_) {
      if (!keep_going(control, status.scanned, status.total)) {
        status.cancelled = true;
        break;
      }
      const std::size_t want = std::min<std::size_t>(
          kChunk, static_cast<std::size_t>(slice.end - cursor));
      const auto io = memory_->read(
          cursor, std::span<std::byte>(window.data() + carry, want));
      if (io.error == EPERM || io.error == EACCES) {
        denied = true;
        break;
      }
      if (io.transferred == 0) {
        break;
      }
      any_read = true;
      const std::size_t total = carry + io.transferred;
      const std::uintptr_t base = cursor - carry;
      if (total >= width) {
        for (std::uintptr_t address = align_up(base, query.alignment);
             address + width <= base + total && !truncated_;
             address += query.alignment) {
          const std::byte *data = window.data() + (address - base);
          if (!matches_current(query, data, width)) {
            continue;
          }
          if (!push_hit(address, data, width)) {
            break;
          }
        }
      }
      carry = width > 1 && total >= width - 1 ? width - 1 : 0;
      if (carry != 0) {
        std::memmove(window.data(), window.data() + (total - carry), carry);
      }
      cursor += io.transferred;
      status.scanned += io.transferred;
      if (io.transferred < want) {
        break;
      }
    }
    if (truncated_ || status.cancelled || denied) {
      break;
    }
  }

  if (!any_read && denied) {
    reset();
    status.message =
        "Немає прав читати пам'ять цього процесу. Ядро Yama "
        "дозволяє ptrace лише нащадкам, "
        "тому гру варто запускати з GameX, а не підключатись до вже "
        "відкритого процесу.";
    return status;
  }
  status.ok = true;
  status.truncated = truncated_;
  status.hit_count = addresses_.size();
  if (status.cancelled) {
    status.message = "Пошук зупинено";
  } else if (truncated_) {
    status.message = "Досягнуто ліміт результатів";
  }
  return status;
}

ScanStatus Scanner::scan_structure(const StructureTemplate &tmpl,
                                   const ScanQuery &region,
                                   const ScanControl &control) {
  reset();
  ScanStatus status;
  if (memory_ == nullptr || !memory_->alive()) {
    status.message = "Процес недоступний";
    return status;
  }
  std::string error;
  if (!tmpl.validate(&error)) {
    status.message = error;
    return status;
  }
  const std::size_t span = tmpl.span();
  const std::size_t alignment = region.alignment == 0 ? 1 : region.alignment;
  width_ = static_cast<std::uint32_t>(span);
  const auto slices = slices_for(region, true, span);
  for (const Slice &slice : slices) {
    status.total += static_cast<std::uint64_t>(slice.end - slice.start);
  }

  bool denied = false;
  bool any_read = false;
  constexpr std::size_t kChunk = 1u << 20;
  std::vector<std::byte> window(kChunk + span);

  for (const Slice &slice : slices) {
    std::size_t carry = 0;
    std::uintptr_t cursor = slice.start;
    while (cursor < slice.end && !truncated_) {
      if (!keep_going(control, status.scanned, status.total)) {
        status.cancelled = true;
        break;
      }
      const std::size_t want = std::min<std::size_t>(
          kChunk, static_cast<std::size_t>(slice.end - cursor));
      const auto io = memory_->read(
          cursor, std::span<std::byte>(window.data() + carry, want));
      if (io.error == EPERM || io.error == EACCES) {
        denied = true;
        break;
      }
      if (io.transferred == 0) {
        break;
      }
      any_read = true;
      const std::size_t total = carry + io.transferred;
      const std::uintptr_t base = cursor - carry;
      if (total >= span) {
        for (std::uintptr_t address = align_up(base, alignment);
             address + span <= base + total && !truncated_;
             address += alignment) {
          const std::byte *data = window.data() + (address - base);
          if (!structure_matches(tmpl, data, span)) {
            continue;
          }
          if (!push_hit(address, data, span)) {
            break;
          }
        }
      }
      carry = span > 1 && total >= span - 1 ? span - 1 : 0;
      if (carry != 0) {
        std::memmove(window.data(), window.data() + (total - carry), carry);
      }
      cursor += io.transferred;
      status.scanned += io.transferred;
      if (io.transferred < want) {
        break;
      }
    }
    if (truncated_ || status.cancelled || denied) {
      break;
    }
  }

  if (!any_read && denied) {
    reset();
    status.message =
        "Немає прав читати пам'ять цього процесу. Ядро Yama "
        "дозволяє ptrace лише нащадкам, "
        "тому гру варто запускати з GameX, а не підключатись до вже "
        "відкритого процесу.";
    return status;
  }
  status.ok = true;
  status.truncated = truncated_;
  status.hit_count = addresses_.size();
  if (status.cancelled) {
    status.message = "Пошук зупинено";
  } else if (truncated_) {
    status.message = "Досягнуто ліміт результатів";
  }
  return status;
}

ScanStatus Scanner::refine(const ScanQuery &query, const ScanControl &control) {
  if (memory_ == nullptr || !memory_->alive()) {
    ScanStatus status;
    status.message = "Процес недоступний";
    return status;
  }
  if (query.op == CompareOp::Unknown) {
    ScanStatus status;
    status.message = "Для відсіву оберіть умову: змінилось, зросло, дорівнює "
                     "або інше порівняння";
    return status;
  }
  if (!snapshots_.empty() && addresses_.empty()) {
    return refine_snapshot(query, control);
  }
  if (addresses_.empty()) {
    ScanStatus status;
    status.message = "Спочатку виконайте пошук";
    return status;
  }
  const std::size_t width = value_width(query);
  if (width == 0 || width != width_) {
    ScanStatus status;
    status.message = "Ширина значення не збігається з попереднім пошуком";
    return status;
  }
  if (!relative_op(query.op) && query.value.bytes.size() != width) {
    ScanStatus status;
    status.message = "Значення не зібране";
    return status;
  }
  if (query.op == CompareOp::Between && query.upper.bytes.size() != width) {
    ScanStatus status;
    status.message = "Верхня межа діапазону не зібрана";
    return status;
  }
  return refine_hits(query, control);
}

ScanStatus Scanner::refine_hits(const ScanQuery &query,
                                const ScanControl &control) {
  ScanStatus status;
  const std::size_t width = width_;
  const std::vector<std::uintptr_t> old_addresses = std::move(addresses_);
  const std::vector<std::byte> old_previous = std::move(previous_);
  addresses_.clear();
  previous_.clear();
  width_ = static_cast<std::uint32_t>(width);
  status.total = old_addresses.size();

  constexpr std::uintptr_t kWindow = 1u << 20;
  std::size_t index = 0;
  while (index < old_addresses.size() && !truncated_) {
    if (!keep_going(control, status.scanned, status.total)) {
      status.cancelled = true;
      break;
    }
    const std::uintptr_t origin = old_addresses[index];
    std::size_t end_index = index + 1;
    while (end_index < old_addresses.size() &&
           old_addresses[end_index] >= origin &&
           old_addresses[end_index] + width <= origin + kWindow) {
      ++end_index;
    }
    const std::uintptr_t last = old_addresses[end_index - 1];
    const std::size_t bytes = static_cast<std::size_t>(last - origin) + width;
    std::vector<std::byte> buffer(bytes);
    const auto io = memory_->read(origin, buffer);
    for (std::size_t hit = index; hit < end_index; ++hit) {
      const std::size_t offset =
          static_cast<std::size_t>(old_addresses[hit] - origin);
      if (!io.ok() || offset + width > io.transferred) {
        continue;
      }
      const std::byte *previous = old_previous.data() + hit * width;
      const std::byte *current = buffer.data() + offset;
      if (!matches_previous(query, previous, current, width)) {
        continue;
      }
      if (!push_hit(old_addresses[hit], current, width)) {
        break;
      }
    }
    status.scanned += end_index - index;
    index = end_index;
  }

  status.ok = true;
  status.cancelled = status.cancelled;
  status.truncated = truncated_;
  status.hit_count = addresses_.size();
  if (status.cancelled) {
    status.message = "Відсів зупинено";
  }
  return status;
}

ScanStatus Scanner::refine_snapshot(const ScanQuery &query,
                                    const ScanControl &control) {
  ScanStatus status;
  const std::size_t width = value_width(query);
  if (width == 0 || query.alignment == 0) {
    status.message = "Порожнє значення або нульове вирівнювання";
    return status;
  }
  if (!relative_op(query.op) && query.op != CompareOp::NotEqual &&
      query.value.bytes.size() != width) {
    status.message = "Значення не зібране";
    return status;
  }
  if (query.op == CompareOp::Between && query.upper.bytes.size() != width) {
    status.message = "Верхня межа діапазону не зібрана";
    return status;
  }
  if (!is_fixed_width(query.type) && relative_op(query.op) &&
      query.op != CompareOp::Changed && query.op != CompareOp::Unchanged) {
    status.message = "Для рядка доступні умови «змінилось» і «не змінилось»";
    return status;
  }

  for (const Snapshot &snapshot : snapshots_) {
    status.total += snapshot.data.size();
  }
  width_ = static_cast<std::uint32_t>(width);
  constexpr std::size_t kChunk = 1u << 20;
  std::vector<std::byte> live(kChunk + width);

  for (const Snapshot &snapshot : snapshots_) {
    if (snapshot.data.size() < width) {
      continue;
    }
    std::size_t offset = 0;
    while (offset < snapshot.data.size() && !truncated_) {
      if (!keep_going(control, status.scanned, status.total)) {
        status.cancelled = true;
        break;
      }
      const std::size_t want = std::min(kChunk, snapshot.data.size() - offset);
      const auto io = memory_->read(snapshot.base + offset,
                                    std::span<std::byte>(live.data(), want));
      if (io.transferred < width) {
        break;
      }
      const std::uintptr_t base = snapshot.base + offset;
      const std::size_t readable = std::min(io.transferred, want);
      for (std::uintptr_t address = align_up(base, query.alignment);
           address + width <= base + readable && !truncated_;
           address += query.alignment) {
        const std::size_t local = static_cast<std::size_t>(address - base);
        const std::size_t absolute = offset + local;
        if (absolute + width > snapshot.data.size()) {
          break;
        }
        const std::byte *previous = snapshot.data.data() + absolute;
        const std::byte *current = live.data() + local;
        if (!matches_previous(query, previous, current, width)) {
          continue;
        }
        if (!push_hit(address, current, width)) {
          break;
        }
      }
      const std::size_t step =
          readable > width ? readable - (width - 1) : readable;
      offset += step;
      status.scanned += step;
      if (io.transferred < want) {
        break;
      }
    }
    if (status.cancelled || truncated_) {
      break;
    }
  }

  snapshots_.clear();
  status.ok = true;
  status.truncated = truncated_;
  status.hit_count = addresses_.size();
  if (status.cancelled) {
    status.message = "Відсів зупинено";
  } else if (truncated_) {
    status.message = "Досягнуто ліміт результатів";
  }
  return status;
}

} // namespace gamex
