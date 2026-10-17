#pragma once

#include <QDialog>

#include <sys/types.h>
#include <vector>

class QLineEdit;
class QTableWidget;

namespace gamex {

struct ProcessInfo;

class ProcessDialog : public QDialog {
  Q_OBJECT

public:
  explicit ProcessDialog(QWidget *parent = nullptr);

  pid_t selected_pid() const { return selected_pid_; }

private:
  void reload();
  void apply_filter();
  void accept_selection();

  QLineEdit *filter_ = nullptr;
  QTableWidget *table_ = nullptr;
  std::vector<ProcessInfo> processes_;
  pid_t selected_pid_ = -1;
};

} // namespace gamex
