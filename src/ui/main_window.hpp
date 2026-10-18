#pragma once

#include "core/address_table.hpp"
#include "core/dump_book.hpp"
#include "core/emulator_profile.hpp"
#include "core/freezer.hpp"
#include "core/process_memory.hpp"
#include "core/scanner.hpp"
#include "core/structure.hpp"
#include "core/value_codec.hpp"

#include <QMainWindow>
#include <QTableWidget>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;

class GamexTable : public QTableWidget {
public:
  using QTableWidget::QTableWidget;

  bool is_editing() const { return state() == EditingState; }
};

namespace gamex {

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private:
  enum TableColumn {
    ColHold = 0,
    ColMode = 1,
    ColGroup = 2,
    ColDescription = 3,
    ColBinding = 4,
    ColAddress = 5,
    ColType = 6,
    ColClampMin = 7,
    ColClampMax = 8,
    ColValue = 9,
    ColCount = 10,
  };

  void build_ui();
  void build_menus();
  void choose_process();
  void launch_process();
  void attach_pid(pid_t pid, const QString &title);
  void disconnect_process();
  void run_scan(bool refine);
  void start_scan_worker(ScanQuery query, bool refine,
                         std::optional<StructureTemplate> structure);
  void wait_for_scan_thread();
  void reset_scan();
  void add_selected_hit();
  void remove_selected_entries();
  void save_table();
  void load_table();
  void refresh_views();
  void on_table_changed(QTableWidgetItem *item);
  void choose_region();
  void stop_scan();
  void on_scan_progress(qulonglong scanned, qulonglong total);
  void on_scan_completed();
  void set_scanning(bool scanning);
  void detach_process(const QString &reason);
  ScanQuery current_query(std::string *error) const;
  void show_hits();
  void show_entries();
  void apply_group_filter();
  void update_value_fields();
  void update_dump_indicators();
  void write_status(const QString &text);
  void toggle_freeze_selected();
  void set_value_selected();
  void cycle_freeze_mode_selected();
  void capture_value_selected();
  void bind_selected_to_module();
  void find_pointers_for_selected();
  void open_hex_for_selected();
  void open_structure_dialog();
  void open_watch_for_selected();
  void capture_dump_slot(int slot);
  void refine_by_formula();
  void open_help();
  void open_about();
  void open_about_qt();
  void on_emulator_changed();
  void detect_emulator_ram();
  const EmulatorProfile *current_emulator() const;
  bool bind_entry_to_emulator(AddressEntry &entry, std::uintptr_t host_address,
                              std::string *error);
  bool try_enable_hold(AddressEntry &entry, std::string *error);
  std::size_t resolve_addresses();
  std::vector<int> selected_entry_rows() const;
  void install_mode_combo(int row, FreezeMode mode);
  QString format_hit_value(const ScanHit &hit) const;

  QLineEdit *value_edit_ = nullptr;
  QLineEdit *upper_edit_ = nullptr;
  QLabel *value_label_ = nullptr;
  QLabel *upper_label_ = nullptr;
  QComboBox *type_combo_ = nullptr;
  QComboBox *endian_combo_ = nullptr;
  QComboBox *op_combo_ = nullptr;
  QComboBox *emulator_combo_ = nullptr;
  QLabel *emulator_ram_label_ = nullptr;
  QSpinBox *alignment_ = nullptr;
  QSpinBox *max_region_mib_ = nullptr;
  QCheckBox *writable_only_ = nullptr;
  QLabel *process_label_ = nullptr;
  QLabel *region_label_ = nullptr;
  QLabel *hits_hint_ = nullptr;
  QLabel *table_hint_ = nullptr;
  QLineEdit *group_filter_ = nullptr;
  QLineEdit *formula_edit_ = nullptr;
  QPushButton *scan_button_ = nullptr;
  QPushButton *refine_button_ = nullptr;
  QPushButton *stop_button_ = nullptr;
  std::array<QPushButton *, kDumpSlots> dump_buttons_{};
  QProgressBar *progress_ = nullptr;
  QTableWidget *results_ = nullptr;
  GamexTable *table_ = nullptr;
  QTimer *timer_ = nullptr;
  QProcess *child_ = nullptr;
  QThread *scan_thread_ = nullptr;

  std::unique_ptr<ProcessMemory> memory_;
  std::unique_ptr<Scanner> scanner_;
  AddressTable addresses_;
  DumpBook dumps_;
  Freezer freezer_;
  std::optional<StructureTemplate> last_structure_;
  ValueType last_scan_type_ = ValueType::Int32;
  Endian last_scan_endian_ = Endian::Little;
  ScanStatus last_status_;
  std::mutex status_mutex_;
  std::atomic<bool> cancel_{false};
  bool limit_region_ = false;
  std::uintptr_t region_start_ = 0;
  std::uintptr_t region_end_ = 0;
  pid_t launched_pid_ = -1;
  bool updating_ = false;
  bool scanning_ = false;
  bool ignore_child_errors_ = false;
};

} // namespace gamex
