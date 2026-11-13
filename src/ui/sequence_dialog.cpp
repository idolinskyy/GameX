#include "ui/sequence_dialog.hpp"

#include "core/search_value.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace gamex {
namespace {

void add_type(QComboBox *combo, const char *label, ValueType type) {
  combo->addItem(QString::fromUtf8(label), static_cast<int>(type));
}

} // namespace

SequenceDialog::SequenceDialog(ValueType type, Endian endian, int alignment,
                               const QString &initial_text, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle("Послідовність значень");
  resize(520, 220);

  auto *hint = new QLabel(
      "Шукає кілька чисел <b>одного типу підряд</b> у пам'яті "
      "(типово ресурси на панелі: золото, залізо, дрова…).<br>"
      "Приклад: <code>1000 555 333 5555</code> — чотири цілі по 4 байти "
      "зі зсувами 0, 4, 8, 12. Якщо в грі між полями є дірки — "
      "краще «Структура…».",
      this);
  hint->setWordWrap(true);
  hint->setTextFormat(Qt::RichText);

  values_edit_ = new QLineEdit(this);
  values_edit_->setPlaceholderText("1000 555 333 5555");
  values_edit_->setText(initial_text);
  values_edit_->setClearButtonEnabled(true);

  type_combo_ = new QComboBox(this);
  add_type(type_combo_, "Ціле 1 байт", ValueType::Int8);
  add_type(type_combo_, "Беззнакове 1 байт", ValueType::UInt8);
  add_type(type_combo_, "Ціле 2 байти", ValueType::Int16);
  add_type(type_combo_, "Беззнакове 2 байти", ValueType::UInt16);
  add_type(type_combo_, "Ціле 3 байти", ValueType::Int24);
  add_type(type_combo_, "Ціле 4 байти", ValueType::Int32);
  add_type(type_combo_, "Беззнакове 4 байти", ValueType::UInt32);
  add_type(type_combo_, "Ціле 8 байтів", ValueType::Int64);
  add_type(type_combo_, "Беззнакове 8 байтів", ValueType::UInt64);
  add_type(type_combo_, "Дріб 4 байти", ValueType::Float32);
  add_type(type_combo_, "Дріб 8 байтів", ValueType::Float64);
  const int type_index = type_combo_->findData(static_cast<int>(type));
  type_combo_->setCurrentIndex(type_index >= 0 ? type_index : 6);

  endian_combo_ = new QComboBox(this);
  endian_combo_->addItem("Молодший байт спочатку",
                         static_cast<int>(Endian::Little));
  endian_combo_->addItem("Старший байт спочатку",
                         static_cast<int>(Endian::Big));
  endian_combo_->setCurrentIndex(endian == Endian::Big ? 1 : 0);

  alignment_ = new QSpinBox(this);
  alignment_->setRange(1, 4096);
  alignment_->setValue(alignment > 0 ? alignment : 4);
  alignment_->setPrefix("крок ");
  alignment_->setToolTip(
      "Крок перебору базової адреси. Зазвичай = ширина типу (для int32 — 4).");

  auto *form = new QFormLayout();
  form->addRow("Значення:", values_edit_);
  form->addRow("Тип кожного:", type_combo_);
  form->addRow("Порядок байтів:", endian_combo_);
  form->addRow("Крок адреси:", alignment_);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText("Шукати");
  buttons->button(QDialogButtonBox::Cancel)->setText("Скасувати");
  connect(buttons, &QDialogButtonBox::accepted, this,
          &SequenceDialog::on_accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(hint);
  layout->addLayout(form);
  layout->addWidget(buttons);

  values_edit_->setFocus();
  values_edit_->selectAll();
}

std::size_t SequenceDialog::alignment() const {
  return static_cast<std::size_t>(alignment_->value());
}

void SequenceDialog::on_accept() {
  const auto type = static_cast<ValueType>(type_combo_->currentData().toInt());
  const auto endian = static_cast<Endian>(endian_combo_->currentData().toInt());
  std::string error;
  auto tmpl = make_sequence_structure(
      "Sequence", type, endian, values_edit_->text().toStdString(), &error);
  if (!tmpl) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  structure_ = std::move(*tmpl);
  accept();
}

} // namespace gamex
