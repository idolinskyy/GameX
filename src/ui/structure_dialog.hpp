#pragma once

#include "core/address_table.hpp"
#include "core/process_memory.hpp"
#include "core/structure.hpp"

#include <QDialog>

#include <optional>
#include <vector>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace gamex {

class StructureDialog : public QDialog {
  Q_OBJECT

public:
  StructureDialog(ProcessMemory *memory,
                  const std::vector<AddressEntry> &draft_entries,
                  QWidget *parent = nullptr);

  const StructureTemplate &structure() const { return structure_; }
  std::size_t alignment() const;

private:
  void fill_type_combo(QComboBox *combo, ValueType selected);
  void add_field_row(const StructureField &field);
  void on_add_row();
  void on_remove_row();
  void on_draft_from_table();
  void on_accept();
  std::optional<StructureTemplate> collect(std::string *error) const;

  ProcessMemory *memory_ = nullptr;
  std::vector<AddressEntry> draft_entries_;
  StructureTemplate structure_;
  QLineEdit *name_edit_ = nullptr;
  QComboBox *endian_combo_ = nullptr;
  QSpinBox *alignment_ = nullptr;
  QTableWidget *fields_ = nullptr;
};

} // namespace gamex
