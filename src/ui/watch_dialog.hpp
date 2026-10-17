#pragma once

#include "core/watchpoint.hpp"

#include <QDialog>

#include <atomic>
#include <cstdint>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace gamex {

class WatchDialog : public QDialog {
  Q_OBJECT

public:
  WatchDialog(pid_t pid, std::uintptr_t address, std::size_t size,
              QWidget *parent = nullptr);
  ~WatchDialog() override;

private:
  void on_wait();
  void on_step();
  void on_until_ret();
  void on_stop_wait();
  void show_hit(const WatchHit &hit);
  void set_busy(bool busy);
  void append_log(const QString &text);
  bool ensure_attached(std::string *error);

  pid_t pid_ = -1;
  std::uintptr_t address_ = 0;
  std::size_t size_ = 4;
  WatchSession session_;
  std::atomic<bool> cancel_{false};
  bool waiting_ = false;

  QLabel *status_ = nullptr;
  QPlainTextEdit *log_ = nullptr;
  QPushButton *wait_button_ = nullptr;
  QPushButton *stop_button_ = nullptr;
  QPushButton *step_button_ = nullptr;
  QPushButton *ret_button_ = nullptr;
};

} // namespace gamex
