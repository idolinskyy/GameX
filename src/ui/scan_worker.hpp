#pragma once

#include "core/scanner.hpp"
#include "core/structure.hpp"

#include <QObject>

#include <atomic>
#include <mutex>
#include <optional>

namespace gamex {

class ScanWorker : public QObject {
  Q_OBJECT

public:
  ScanWorker(Scanner *scanner, ScanQuery query, bool refine,
             std::atomic<bool> *cancel, ScanStatus *status,
             std::mutex *status_mutex,
             std::optional<StructureTemplate> structure = std::nullopt);

public slots:
  void start();

signals:
  void progress(qulonglong scanned, qulonglong total);
  void completed();

private:
  Scanner *scanner_ = nullptr;
  ScanQuery query_;
  bool refine_ = false;
  std::atomic<bool> *cancel_ = nullptr;
  ScanStatus *status_ = nullptr;
  std::mutex *status_mutex_ = nullptr;
  std::optional<StructureTemplate> structure_;
};

} // namespace gamex
