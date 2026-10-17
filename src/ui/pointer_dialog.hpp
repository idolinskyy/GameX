#pragma once

#include "core/pointer_scan.hpp"
#include "core/process_memory.hpp"

#include <QDialog>

#include <vector>

class QSpinBox;
class QTableWidget;

namespace gamex {

class PointerDialog : public QDialog {
    Q_OBJECT

public:
    PointerDialog(ProcessMemory* memory, std::uintptr_t target, QWidget* parent = nullptr);

    const std::vector<PointerPath>& selected_paths() const { return selected_; }

private:
    void run_scan();
    void accept_selection();

    ProcessMemory* memory_ = nullptr;
    std::uintptr_t target_ = 0;
    QSpinBox* depth_ = nullptr;
    QTableWidget* table_ = nullptr;
    std::vector<PointerPath> paths_;
    std::vector<PointerPath> selected_;
};

}  // namespace gamex
