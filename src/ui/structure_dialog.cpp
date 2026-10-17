#include "ui/structure_dialog.hpp"

#include "core/search_value.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gamex {
namespace {

void add_type(QComboBox *combo, const char *label, ValueType type) {
  combo->addItem(QString::fromUtf8(label), static_cast<int>(type));
}

} // namespace

StructureDialog::StructureDialog(ProcessMemory *memory,
                                 const std::vector<AddressEntry> &draft_entries,
                                 QWidget *parent)
    : QDialog(parent), memory_(memory), draft_entries_(draft_entries) {
  setWindowTitle("Структура");
  resize(720, 420);

  auto *hint = new QLabel(
      "Шаблон полів зі зсувами. Дірки дозволені (наприклад health@0, gold@8). "
      "«З таблиці» будує чорновик з виділених адрес, якщо вони лежать поруч.",
      this);
  hint->setWordWrap(true);

  name_edit_ = new QLineEdit(this);
  name_edit_->setPlaceholderText("Ім'я структури");
  name_edit_->setText("Character");

  endian_combo_ = new QComboBox(this);
  endian_combo_->addItem("Молодший байт спочатку",
                         static_cast<int>(Endian::Little));
  endian_combo_->addItem("Старший байт спочатку",
                         static_cast<int>(Endian::Big));

  alignment_ = new QSpinBox(this);
  alignment_->setRange(1, 4096);
  alignment_->setValue(4);
  alignment_->setPrefix("крок ");

  fields_ = new QTableWidget(this);
  fields_->setColumnCount(4);
  fields_->setHorizontalHeaderLabels({"Зсув", "Тип", "Значення", "Опис"});
  fields_->verticalHeader()->setVisible(false);
  fields_->horizontalHeader()->setStretchLastSection(true);
  fields_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  fields_->setSelectionBehavior(QAbstractItemView::SelectRows);

  auto *add_row = new QPushButton("Додати поле", this);
  auto *remove_row = new QPushButton("Прибрати поле", this);
  auto *from_table = new QPushButton("З таблиці", this);
  from_table->setEnabled(draft_entries_.size() >= 2);
  from_table->setToolTip(
      "Побудувати чорновик з виділених рядків головної таблиці");

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText("Шукати");

  connect(add_row, &QPushButton::clicked, this, &StructureDialog::on_add_row);
  connect(remove_row, &QPushButton::clicked, this,
          &StructureDialog::on_remove_row);
  connect(from_table, &QPushButton::clicked, this,
          &StructureDialog::on_draft_from_table);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &StructureDialog::on_accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *top = new QHBoxLayout();
  top->addWidget(new QLabel("Ім'я:", this));
  top->addWidget(name_edit_, 1);
  top->addWidget(endian_combo_);
  top->addWidget(alignment_);

  auto *tools = new QHBoxLayout();
  tools->addWidget(add_row);
  tools->addWidget(remove_row);
  tools->addWidget(from_table);
  tools->addStretch();

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(hint);
  layout->addLayout(top);
  layout->addLayout(tools);
  layout->addWidget(fields_);
  layout->addWidget(buttons);

  if (draft_entries_.size() >= 2) {
    on_draft_from_table();
  } else {
    StructureField health;
    health.offset = 0;
    health.type = ValueType::Int32;
    health.description = "health";
    StructureField gold;
    gold.offset = 8;
    gold.type = ValueType::Int32;
    gold.description = "gold";
    add_field_row(health);
    add_field_row(gold);
  }
}

std::size_t StructureDialog::alignment() const {
  return static_cast<std::size_t>(alignment_->value());
}

void StructureDialog::fill_type_combo(QComboBox *combo, ValueType selected) {
  add_type(combo, "Ціле 1 байт", ValueType::Int8);
  add_type(combo, "Беззнакове 1 байт", ValueType::UInt8);
  add_type(combo, "Ціле 2 байти", ValueType::Int16);
  add_type(combo, "Беззнакове 2 байти", ValueType::UInt16);
  add_type(combo, "Ціле 3 байти", ValueType::Int24);
  add_type(combo, "Ціле 4 байти", ValueType::Int32);
  add_type(combo, "Беззнакове 4 байти", ValueType::UInt32);
  add_type(combo, "Ціле 8 байтів", ValueType::Int64);
  add_type(combo, "Беззнакове 8 байтів", ValueType::UInt64);
  add_type(combo, "Дріб 4 байти", ValueType::Float32);
  add_type(combo, "Дріб 8 байтів", ValueType::Float64);
  add_type(combo, "Байти", ValueType::ByteArray);
  for (int i = 0; i < combo->count(); ++i) {
    if (combo->itemData(i).toInt() == static_cast<int>(selected)) {
      combo->setCurrentIndex(i);
      return;
    }
  }
  combo->setCurrentIndex(5);
}

void StructureDialog::add_field_row(const StructureField &field) {
  const int row = fields_->rowCount();
  fields_->insertRow(row);
  fields_->setItem(row, 0,
                   new QTableWidgetItem(
                       QString::number(static_cast<qulonglong>(field.offset))));
  auto *type = new QComboBox(fields_);
  fill_type_combo(type, field.type);
  fields_->setCellWidget(row, 1, type);
  QString value;
  if (!field.value.bytes.empty()) {
    value = QString::fromStdString(
        decode_text(field.type, Endian::Little, field.value.bytes));
  }
  fields_->setItem(row, 2, new QTableWidgetItem(value));
  fields_->setItem(
      row, 3, new QTableWidgetItem(QString::fromStdString(field.description)));
}

void StructureDialog::on_add_row() {
  StructureField field;
  field.offset = 0;
  if (fields_->rowCount() > 0) {
    const auto *prev = fields_->item(fields_->rowCount() - 1, 0);
    if (prev != nullptr) {
      field.offset = static_cast<std::size_t>(prev->text().toULongLong()) + 4;
    }
  }
  field.type = ValueType::Int32;
  add_field_row(field);
}

void StructureDialog::on_remove_row() {
  const auto rows = fields_->selectionModel()->selectedRows();
  for (int i = static_cast<int>(rows.size()) - 1; i >= 0; --i) {
    fields_->removeRow(rows[i].row());
  }
}

void StructureDialog::on_draft_from_table() {
  if (draft_entries_.size() < 2) {
    QMessageBox::information(this, "GameX",
                             "Виділіть щонайменше два рядки в таблиці адрес");
    return;
  }
  std::vector<StructureDraftSource> sources;
  sources.reserve(draft_entries_.size());
  for (const AddressEntry &entry : draft_entries_) {
    if (!entry.resolved) {
      QMessageBox::warning(this, "GameX",
                           "Серед виділених є нерозв'язані адреси");
      return;
    }
    StructureDraftSource src;
    src.address = entry.address;
    src.type = entry.type;
    src.endian = entry.endian;
    src.description = entry.description;
    if (!entry.frozen_value.empty()) {
      src.value.bytes = entry.frozen_value;
    } else if (memory_ != nullptr && is_fixed_width(entry.type)) {
      src.value.bytes.resize(fixed_width(entry.type));
      const auto io = memory_->read(entry.address, src.value.bytes);
      if (io.transferred != src.value.bytes.size()) {
        QMessageBox::warning(
            this, "GameX",
            QString("Не вдалося прочитати 0x%1")
                .arg(static_cast<qulonglong>(entry.address), 0, 16));
        return;
      }
    } else {
      QMessageBox::warning(this, "GameX",
                           "Немає значення для побудови чорновика");
      return;
    }
    sources.push_back(std::move(src));
  }
  std::string error;
  auto drafted =
      draft_structure(name_edit_->text().toStdString(), sources, &error);
  if (!drafted) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  endian_combo_->setCurrentIndex(drafted->endian == Endian::Little ? 0 : 1);
  fields_->setRowCount(0);
  for (const StructureField &field : drafted->fields) {
    add_field_row(field);
  }
}

std::optional<StructureTemplate>
StructureDialog::collect(std::string *error) const {
  StructureTemplate tmpl;
  tmpl.name = name_edit_->text().trimmed().toStdString();
  tmpl.endian = static_cast<Endian>(endian_combo_->currentData().toInt());
  for (int row = 0; row < fields_->rowCount(); ++row) {
    StructureField field;
    const auto *offset_item = fields_->item(row, 0);
    const auto *value_item = fields_->item(row, 2);
    const auto *desc_item = fields_->item(row, 3);
    auto *type_combo = qobject_cast<QComboBox *>(fields_->cellWidget(row, 1));
    if (offset_item == nullptr || value_item == nullptr ||
        type_combo == nullptr) {
      if (error) {
        *error = "Неповний рядок поля";
      }
      return std::nullopt;
    }
    bool ok = false;
    field.offset = static_cast<std::size_t>(
        offset_item->text().trimmed().toULongLong(&ok));
    if (!ok) {
      if (error) {
        *error = "Некоректний зсув";
      }
      return std::nullopt;
    }
    field.type = static_cast<ValueType>(type_combo->currentData().toInt());
    field.description =
        desc_item ? desc_item->text().toStdString() : std::string{};
    auto prepared = prepare_search_value(
        field.type, tmpl.endian, value_item->text().toStdString(), error);
    if (!prepared) {
      return std::nullopt;
    }
    field.value = std::move(prepared->value);
    tmpl.fields.push_back(std::move(field));
  }
  if (!tmpl.validate(error)) {
    return std::nullopt;
  }
  return tmpl;
}

void StructureDialog::on_accept() {
  std::string error;
  auto tmpl = collect(&error);
  if (!tmpl) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  structure_ = std::move(*tmpl);
  accept();
}

} // namespace gamex
