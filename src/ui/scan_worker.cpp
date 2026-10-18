#include "ui/scan_worker.hpp"

#include <chrono>

namespace gamex {

ScanWorker::ScanWorker(Scanner *scanner, ScanQuery query, bool refine,
                       std::atomic<bool> *cancel, ScanStatus *status,
                       std::mutex *status_mutex,
                       std::optional<StructureTemplate> structure)
    : scanner_(scanner), query_(std::move(query)), refine_(refine),
      cancel_(cancel), status_(status), status_mutex_(status_mutex),
      structure_(std::move(structure)) {}

void ScanWorker::start() {
  auto last_emit =
      std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
  ScanControl control = [this, &last_emit](std::uint64_t scanned,
                                           std::uint64_t total) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_emit >= std::chrono::milliseconds(80)) {
      last_emit = now;
      emit progress(static_cast<qulonglong>(scanned),
                    static_cast<qulonglong>(total));
    }
    return cancel_ == nullptr || !cancel_->load();
  };
  ScanStatus status;
  if (structure_) {
    status = scanner_->scan_structure(*structure_, query_, control);
  } else if (refine_) {
    status = scanner_->refine(query_, control);
  } else {
    status = scanner_->scan(query_, control);
  }
  if (status_ != nullptr && status_mutex_ != nullptr) {
    std::lock_guard lock(*status_mutex_);
    *status_ = status;
  }
  emit progress(static_cast<qulonglong>(status.scanned),
                static_cast<qulonglong>(status.total));
  emit completed();
}

} // namespace gamex
