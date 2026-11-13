#pragma once

#include "core/structure.hpp"
#include "core/value_codec.hpp"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;

namespace gamex {

class SequenceDialog : public QDialog {
  Q_OBJECT

public:
  SequenceDialog(ValueType type, Endian endian, int alignment,
                 const QString &initial_text, QWidget *parent = nullptr);

  const StructureTemplate &structure() const { return structure_; }
  std::size_t alignment() const;

private:
  void on_accept();

  StructureTemplate structure_;
  QLineEdit *values_edit_ = nullptr;
  QComboBox *type_combo_ = nullptr;
  QComboBox *endian_combo_ = nullptr;
  QSpinBox *alignment_ = nullptr;
};

} // namespace gamex
