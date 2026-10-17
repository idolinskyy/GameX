#include "ui/hex_dialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <charconv>

namespace gamex {
namespace {

constexpr int kColumns = 16;
constexpr int kRows = 16;

std::optional<std::uintptr_t> parse_addr(const QString& text) {
    QString body = text.trimmed();
    if (body.startsWith("0x") || body.startsWith("0X")) {
        body = body.mid(2);
    }
    if (body.isEmpty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const QByteArray utf = body.toUtf8();
    const auto result = std::from_chars(utf.constData(), utf.constData() + utf.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != utf.constData() + utf.size()) {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(value);
}

}  // namespace

HexDialog::HexDialog(ProcessMemory* memory, std::uintptr_t address, QWidget* parent)
    : QDialog(parent), memory_(memory), base_(address & ~std::uintptr_t{0xf}) {
    setWindowTitle("Пам'ять");
    resize(760, 520);

    address_edit_ = new QLineEdit(this);
    address_edit_->setText(QString("0x%1").arg(static_cast<qulonglong>(base_), 0, 16));
    auto *go = new QPushButton("Перейти", this);
    auto *prev = new QPushButton("−256", this);
    auto *next = new QPushButton("+256", this);

    table_ = new QTableWidget(kRows, kColumns + 1, this);
    QStringList headers;
    headers << "Адреса";
    for (int col = 0; col < kColumns; ++col) {
        headers << QString("%1").arg(col, 2, 16, QChar('0')).toUpper();
    }
    table_->setHorizontalHeaderLabels(headers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int col = 1; col <= kColumns; ++col) {
        table_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }

    auto *hint = new QLabel(
        "Двічі клацніть байт, змініть hex (наприклад 7F) і Enter — запис у процес.", this);
    hint->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(go, &QPushButton::clicked, this, [this] {
        const auto address = parse_addr(address_edit_->text());
        if (!address) {
            QMessageBox::warning(this, "GameX", "Некоректна адреса");
            return;
        }
        base_ = *address & ~std::uintptr_t{0xf};
        address_edit_->setText(QString("0x%1").arg(static_cast<qulonglong>(base_), 0, 16));
        reload();
    });
    connect(prev, &QPushButton::clicked, this, [this] { go_relative(-256); });
    connect(next, &QPushButton::clicked, this, [this] { go_relative(256); });
    connect(table_, &QTableWidget::itemChanged, this, &HexDialog::on_cell_changed);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel("Адреса", this));
    top->addWidget(address_edit_, 1);
    top->addWidget(go);
    top->addWidget(prev);
    top->addWidget(next);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(hint);
    layout->addWidget(table_, 1);
    layout->addWidget(buttons);
    reload();
}

void HexDialog::go_relative(int delta) {
    if (delta < 0 && base_ < static_cast<std::uintptr_t>(-delta)) {
        base_ = 0;
    } else {
        base_ = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(base_) + delta);
    }
    base_ &= ~std::uintptr_t{0xf};
    address_edit_->setText(QString("0x%1").arg(static_cast<qulonglong>(base_), 0, 16));
    reload();
}

void HexDialog::reload() {
    if (memory_ == nullptr) {
        return;
    }
    updating_ = true;
    table_->blockSignals(true);
    constexpr std::size_t kBytes = static_cast<std::size_t>(kRows * kColumns);
    std::vector<std::byte> buffer(kBytes);
    const auto io = memory_->read(base_, buffer);
    for (int row = 0; row < kRows; ++row) {
        const std::uintptr_t row_addr = base_ + static_cast<std::uintptr_t>(row * kColumns);
        auto *addr = new QTableWidgetItem(QString("0x%1").arg(static_cast<qulonglong>(row_addr), 0, 16));
        addr->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table_->setItem(row, 0, addr);
        for (int col = 0; col < kColumns; ++col) {
            const std::size_t index = static_cast<std::size_t>(row * kColumns + col);
            QString text = "??";
            if (index < io.transferred) {
                text = QString("%1")
                           .arg(std::to_integer<unsigned>(buffer[index]), 2, 16, QChar('0'))
                           .toUpper();
            }
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, static_cast<qulonglong>(row_addr + static_cast<unsigned>(col)));
            if (index >= io.transferred) {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            table_->setItem(row, col + 1, item);
        }
    }
    table_->blockSignals(false);
    updating_ = false;
}

void HexDialog::on_cell_changed(QTableWidgetItem* item) {
    if (updating_ || item == nullptr || item->column() == 0 || memory_ == nullptr) {
        return;
    }
    QString text = item->text().trimmed();
    if (text.startsWith("0x") || text.startsWith("0X")) {
        text = text.mid(2);
    }
    if (text.size() != 2) {
        QMessageBox::warning(this, "GameX", "Потрібен один байт, наприклад 7F");
        reload();
        return;
    }
    bool ok = false;
    const int value = text.toInt(&ok, 16);
    if (!ok || value < 0 || value > 255) {
        QMessageBox::warning(this, "GameX", "Некоректний байт");
        reload();
        return;
    }
    const auto address = static_cast<std::uintptr_t>(item->data(Qt::UserRole).toULongLong());
    std::byte byte{static_cast<unsigned char>(value)};
    const auto io = memory_->write(address, std::span<const std::byte>(&byte, 1));
    if (!io.ok() || io.transferred != 1) {
        QMessageBox::warning(this, "GameX", "Не вдалося записати байт");
    }
    reload();
}

}  // namespace gamex
