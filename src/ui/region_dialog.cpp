#include "ui/region_dialog.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gamex {
namespace {

QString size_text(std::size_t bytes) {
  if (bytes >= (1u << 20)) {
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f',
                           1) +
           " МБ";
  }
  if (bytes >= (1u << 10)) {
    return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1) + " КБ";
  }
  return QString::number(static_cast<qulonglong>(bytes)) + " Б";
}

} // namespace

RegionDialog::RegionDialog(std::vector<MemoryRegion> regions, QWidget *parent)
    : QDialog(parent), regions_(std::move(regions)) {
  setWindowTitle("Блок пам'яті");
  resize(860, 480);

  table_ = new QTableWidget(this);
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels({"Адреса", "Розмір", "Права", "Шлях"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setRowCount(static_cast<int>(regions_.size()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    const MemoryRegion &region = regions_[static_cast<std::size_t>(row)];
    const QString range = QString("0x%1 – 0x%2")
                              .arg(static_cast<qulonglong>(region.start), 0, 16)
                              .arg(static_cast<qulonglong>(region.end), 0, 16);
    auto *address = new QTableWidgetItem(range);
    address->setData(Qt::UserRole, row);
    table_->setItem(row, 0, address);
    table_->setItem(row, 1, new QTableWidgetItem(size_text(region.size())));
    QString perms;
    perms += region.readable ? 'r' : '-';
    perms += region.writable ? 'w' : '-';
    perms += region.executable ? 'x' : '-';
    table_->setItem(row, 2, new QTableWidgetItem(perms));
    table_->setItem(
        row, 3, new QTableWidgetItem(QString::fromStdString(region.pathname)));
  }

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto *whole =
      buttons->addButton("Увесь процес", QDialogButtonBox::ActionRole);
  connect(whole, &QPushButton::clicked, this, &RegionDialog::accept_whole);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &RegionDialog::accept_selection);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(table_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *) { accept_selection(); });

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(table_);
  layout->addWidget(buttons);
}

void RegionDialog::accept_selection() {
  const int row = table_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= regions_.size()) {
    return;
  }
  selected_ = regions_[static_cast<std::size_t>(row)];
  whole_ = false;
  accept();
}

void RegionDialog::accept_whole() {
  whole_ = true;
  selected_.reset();
  accept();
}

} // namespace gamex
