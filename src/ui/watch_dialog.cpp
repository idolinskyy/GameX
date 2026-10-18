#include "ui/watch_dialog.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace gamex {

WatchDialog::WatchDialog(pid_t pid, std::uintptr_t address, std::size_t size,
                         QWidget *parent)
    : QDialog(parent), pid_(pid), address_(address),
      size_(size == 0 ? 4 : size) {
  setWindowTitle("Хто пише адресу");
  resize(640, 420);

  auto *warn = new QLabel(
      "Процес працює під ptrace і зупиняється на хіті. Лише процес, запущений "
      "з GameX. На x86_64 RIP у звіті — адреса наступної інструкції після "
      "запису (не самої insn, що писала). Нові потоки підхоплюються через "
      "TRACECLONE. Заміну на NOP ця версія не робить.",
      this);
  warn->setWordWrap(true);
  warn->setStyleSheet("color: #a04000;");

  status_ = new QLabel(QString("Адреса 0x%1, розмір %2.")
                           .arg(static_cast<qulonglong>(address_), 0, 16)
                           .arg(static_cast<qulonglong>(size_)),
                       this);
  status_->setWordWrap(true);

  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setPlaceholderText(
      "HIT: data + rip (після запису), байти наступної insn, мнемоніка "
      "(Capstone).");

  wait_button_ = new QPushButton("Чекати запис", this);
  stop_button_ = new QPushButton("Стоп", this);
  stop_button_->setEnabled(false);
  step_button_ = new QPushButton("Крок", this);
  ret_button_ = new QPushButton("До ret", this);
  step_button_->setEnabled(false);
  ret_button_->setEnabled(false);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

  connect(wait_button_, &QPushButton::clicked, this, &WatchDialog::on_wait);
  connect(stop_button_, &QPushButton::clicked, this,
          &WatchDialog::on_stop_wait);
  connect(step_button_, &QPushButton::clicked, this, &WatchDialog::on_step);
  connect(ret_button_, &QPushButton::clicked, this, &WatchDialog::on_until_ret);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *row = new QHBoxLayout();
  row->addWidget(wait_button_);
  row->addWidget(stop_button_);
  row->addWidget(step_button_);
  row->addWidget(ret_button_);
  row->addStretch();

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(warn);
  layout->addWidget(status_);
  layout->addLayout(row);
  layout->addWidget(log_, 1);
  layout->addWidget(buttons);
}

WatchDialog::~WatchDialog() {
  cancel_.store(true);
  session_.detach();
}

void WatchDialog::set_busy(bool busy) {
  waiting_ = busy;
  wait_button_->setEnabled(!busy);
  stop_button_->setEnabled(busy);
  const bool have_hit =
      !busy && session_.last_hit() != nullptr && session_.stopped();
  step_button_->setEnabled(have_hit);
  ret_button_->setEnabled(have_hit);
}

void WatchDialog::append_log(const QString &text) {
  log_->appendPlainText(text);
}

bool WatchDialog::ensure_attached(std::string *error) {
  if (session_.attached()) {
    return true;
  }
  if (!session_.attach(pid_, error)) {
    return false;
  }
  if (!session_.set_watch(address_, size_, WatchKind::Write, error)) {
    session_.detach();
    return false;
  }
  append_log(QString("Attach + апаратний watch 0x%1 / %2 байт")
                 .arg(static_cast<qulonglong>(address_), 0, 16)
                 .arg(static_cast<qulonglong>(size_)));
  return true;
}

void WatchDialog::show_hit(const WatchHit &hit) {
  QString bytes;
  for (std::byte b : hit.bytes) {
    bytes += QString("%1 ").arg(static_cast<unsigned>(b), 2, 16, QChar('0'));
  }
  QString line =
      QString("HIT tid=%1 data=0x%2 rip(після запису)=0x%3\n  bytes: %4")
          .arg(hit.tid)
          .arg(static_cast<qulonglong>(hit.data_address), 0, 16)
          .arg(static_cast<qulonglong>(hit.instruction), 0, 16)
          .arg(bytes.trimmed());
  if (!hit.disasm.empty()) {
    line += QString("\n  %1").arg(QString::fromStdString(hit.disasm));
  } else {
    line += "\n  (мнемоніка: зберіть з Capstone, зараз лише байти)";
  }
  append_log(line);
  status_->setText(
      QString("Зупинено після запису; наступна insn @ 0x%1. Процес на паузі.")
          .arg(static_cast<qulonglong>(hit.instruction), 0, 16));
  set_busy(false);
}

void WatchDialog::on_wait() {
  if (waiting_) {
    return;
  }
  std::string error;
  if (!ensure_attached(&error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  cancel_.store(false);
  set_busy(true);
  status_->setText("Очікування запису… процес працює під наглядом.");
  append_log("Чекаємо запис у адресу…");

  for (;;) {
    if (cancel_.load()) {
      status_->setText(
          "Очікування скасовано (процес може лишатись під ptrace)");
      set_busy(false);
      return;
    }
    auto hit = session_.wait_hit(200, &cancel_, &error);
    if (hit) {
      show_hit(*hit);
      return;
    }
    if (cancel_.load()) {
      status_->setText("Очікування скасовано");
      set_busy(false);
      return;
    }
    if (error == "Час очікування вичерпано") {
      QApplication::processEvents();
      continue;
    }
    append_log(QString::fromStdString(error.empty() ? "Немає хіта" : error));
    status_->setText(QString::fromStdString(error.empty() ? "Готово" : error));
    set_busy(false);
    return;
  }
}

void WatchDialog::on_stop_wait() { cancel_.store(true); }

void WatchDialog::on_step() {
  std::string error;
  if (!session_.step(&error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  if (session_.last_hit()) {
    show_hit(*session_.last_hit());
  }
}

void WatchDialog::on_until_ret() {
  std::string error;
  if (!session_.run_until_ret(50'000, &error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
  }
  if (session_.last_hit()) {
    append_log("До ret:");
    show_hit(*session_.last_hit());
  }
}

} // namespace gamex
