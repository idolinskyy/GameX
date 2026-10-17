#pragma once

#include "core/memory_region.hpp"

#include <QDialog>

#include <optional>
#include <vector>

class QTableWidget;

namespace gamex {

class RegionDialog : public QDialog {
  Q_OBJECT

public:
  explicit RegionDialog(std::vector<MemoryRegion> regions,
                        QWidget *parent = nullptr);

  bool whole_process() const { return whole_; }
  const MemoryRegion *selected() const {
    return selected_ ? &*selected_ : nullptr;
  }

private:
  void accept_selection();
  void accept_whole();

  QTableWidget *table_ = nullptr;
  std::vector<MemoryRegion> regions_;
  std::optional<MemoryRegion> selected_;
  bool whole_ = false;
};

} // namespace gamex
