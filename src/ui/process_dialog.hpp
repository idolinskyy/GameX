#pragma once

#include <QDialog>

#include <sys/types.h>
#include <unordered_set>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace gamex {

struct ProcessInfo;

class ProcessDialog : public QDialog {
  Q_OBJECT

public:
  explicit ProcessDialog(QWidget *parent = nullptr);
  ~ProcessDialog() override;

  pid_t selected_pid() const { return selected_pid_; }

private:
  void reload();
  void apply_filter();
  void accept_selection();
  void load_settings();
  void save_settings() const;

  QLineEdit *filter_ = nullptr;
  QCheckBox *login_users_only_ = nullptr;
  QCheckBox *my_uid_only_ = nullptr;
  QCheckBox *panel_only_ = nullptr;
  QLabel *empty_hint_ = nullptr;
  QTableWidget *table_ = nullptr;
  std::vector<ProcessInfo> processes_;
  std::unordered_set<pid_t> panel_pids_;
  pid_t selected_pid_ = -1;
};

} // namespace gamex
