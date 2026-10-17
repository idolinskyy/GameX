#include "ui/pointer_dialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gamex {

PointerDialog::PointerDialog(ProcessMemory* memory, std::uintptr_t target, QWidget* parent)
    : QDialog(parent), memory_(memory), target_(target) {
    setWindowTitle("Пошук статичних вказівників");
    resize(820, 480);

    auto *hint = new QLabel(
        QString("Ціль: 0x%1. Шукаємо лише адреси в модулях (статичні). "
                "Динамічний вказівник, який ніде не збережений у модулі, цим пошуком не знайти.")
            .arg(static_cast<qulonglong>(target), 0, 16),
        this);
    hint->setWordWrap(true);

    depth_ = new QSpinBox(this);
    depth_->setRange(1, kMaxPointerDepth);
    depth_->setValue(2);
    depth_->setPrefix("глибина ");

    auto *scan = new QPushButton("Шукати", this);
    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"Прив'язка", "Адреса"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(scan, &QPushButton::clicked, this, &PointerDialog::run_scan);
    connect(buttons, &QDialogButtonBox::accepted, this, &PointerDialog::accept_selection);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
        accept_selection();
    });

    auto *top = new QHBoxLayout();
    top->addWidget(depth_);
    top->addWidget(scan);
    top->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addLayout(top);
    layout->addWidget(table_);
    layout->addWidget(buttons);
}

void PointerDialog::run_scan() {
    if (memory_ == nullptr) {
        return;
    }
    PointerScanner scanner(*memory_);
    const auto status = scanner.scan(target_, depth_->value());
    paths_ = scanner.paths();
    table_->setRowCount(static_cast<int>(paths_.size()));
    for (int row = 0; row < table_->rowCount(); ++row) {
        const PointerPath& path = paths_[static_cast<std::size_t>(row)];
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(format_pointer_path(path))));
        table_->setItem(
            row, 1,
            new QTableWidgetItem(QString("0x%1").arg(static_cast<qulonglong>(path.resolved), 0, 16)));
    }
    if (!status.ok) {
        QMessageBox::warning(this, "GameX", QString::fromStdString(status.message));
        return;
    }
    if (!status.message.empty()) {
        QMessageBox::information(this, "GameX", QString::fromStdString(status.message));
    }
}

void PointerDialog::accept_selection() {
    selected_.clear();
    for (const QTableWidgetSelectionRange& range : table_->selectedRanges()) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            if (row >= 0 && static_cast<std::size_t>(row) < paths_.size()) {
                selected_.push_back(paths_[static_cast<std::size_t>(row)]);
            }
        }
    }
    if (selected_.empty() && table_->currentRow() >= 0 &&
        static_cast<std::size_t>(table_->currentRow()) < paths_.size()) {
        selected_.push_back(paths_[static_cast<std::size_t>(table_->currentRow())]);
    }
    if (selected_.empty()) {
        return;
    }
    accept();
}

}  // namespace gamex
