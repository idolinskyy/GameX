#pragma once

#include "core/process_memory.hpp"

#include <QDialog>
#include <QTableWidgetItem>

#include <cstdint>

class QLineEdit;
class QTableWidget;

namespace gamex {

class HexDialog : public QDialog {
    Q_OBJECT

public:
    HexDialog(ProcessMemory* memory, std::uintptr_t address, QWidget* parent = nullptr);

private:
    void reload();
    void go_relative(int delta);
    void on_cell_changed(QTableWidgetItem* item);

    ProcessMemory* memory_ = nullptr;
    std::uintptr_t base_ = 0;
    QLineEdit* address_edit_ = nullptr;
    QTableWidget* table_ = nullptr;
    bool updating_ = false;
};

}  // namespace gamex
