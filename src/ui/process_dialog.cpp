#include "ui/process_dialog.hpp"

#include "core/process_memory.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gamex {

ProcessDialog::ProcessDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Процеси");
  resize(820, 520);

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText("Пошук за pid, ім'ям або командою");
  filter_->setClearButtonEnabled(true);

  table_ = new QTableWidget(this);
  table_->setColumnCount(3);
  table_->setHorizontalHeaderLabels({"PID", "Ім'я", "Командний рядок"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto *refresh = buttons->addButton("Оновити", QDialogButtonBox::ActionRole);
  connect(refresh, &QPushButton::clicked, this, &ProcessDialog::reload);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &ProcessDialog::accept_selection);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(table_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *) { accept_selection(); });
  connect(filter_, &QLineEdit::textChanged, this,
          [this](const QString &) { apply_filter(); });
  connect(filter_, &QLineEdit::returnPressed, this,
          &ProcessDialog::accept_selection);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(filter_);
  layout->addWidget(table_);
  layout->addWidget(buttons);
  reload();
  filter_->setFocus();
}

void ProcessDialog::reload() {
  processes_ = list_processes();
  apply_filter();
}

void ProcessDialog::apply_filter() {
  const QString needle = filter_->text().trimmed();
  table_->setRowCount(0);
  int row = 0;
  for (const ProcessInfo &info : processes_) {
    const QString pid = QString::number(info.pid);
    const QString name = QString::fromStdString(info.name);
    const QString command = QString::fromStdString(info.command_line);
    const bool match = needle.isEmpty() || pid.contains(needle) ||
                       name.contains(needle, Qt::CaseInsensitive) ||
                       command.contains(needle, Qt::CaseInsensitive);
    if (!match) {
      continue;
    }
    table_->insertRow(row);
    auto *pid_item = new QTableWidgetItem(pid);
    pid_item->setData(Qt::UserRole, static_cast<qlonglong>(info.pid));
    table_->setItem(row, 0, pid_item);
    table_->setItem(row, 1, new QTableWidgetItem(name));
    table_->setItem(row, 2, new QTableWidgetItem(command));
    ++row;
  }
  if (table_->rowCount() > 0) {
    table_->selectRow(0);
  }
}

void ProcessDialog::accept_selection() {
  const int row = table_->currentRow();
  if (row < 0 || table_->item(row, 0) == nullptr) {
    return;
  }
  selected_pid_ =
      static_cast<pid_t>(table_->item(row, 0)->data(Qt::UserRole).toLongLong());
  accept();
}

} // namespace gamex
