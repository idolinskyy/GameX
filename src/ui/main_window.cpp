#include "ui/main_window.hpp"

#include "ui/help_dialog.hpp"
#include "ui/hex_dialog.hpp"
#include "ui/pointer_dialog.hpp"
#include "ui/process_dialog.hpp"
#include "ui/region_dialog.hpp"
#include "ui/scan_worker.hpp"
#include "ui/structure_dialog.hpp"
#include "ui/watch_dialog.hpp"

#include "core/module_map.hpp"
#include "core/search_value.hpp"

#include "gamex_build_info.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <QBrush>
#include <QColor>

#include <unistd.h>

#include <span>
#include <utility>

namespace gamex {
namespace {

QString hex_address(std::uintptr_t address) {
  return QString("0x%1").arg(static_cast<qulonglong>(address), 0, 16);
}

void add_type(QComboBox *combo, const QString &label, ValueType type) {
  combo->addItem(label, static_cast<int>(type));
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  build_ui();
  timer_ = new QTimer(this);
  timer_->setInterval(250);
  connect(timer_, &QTimer::timeout, this, &MainWindow::refresh_views);
  timer_->start();
}

MainWindow::~MainWindow() {
  wait_for_scan_thread();
  if (child_ != nullptr && child_->state() != QProcess::NotRunning) {
    ignore_child_errors_ = true;
    child_->kill();
    child_->waitForFinished(1000);
  }
}

void MainWindow::build_ui() {
  if (elevated_privileges()) {
    setWindowTitle("GameX (root)");
  } else {
    setWindowTitle("GameX");
  }
  resize(1180, 760);

  auto *process_button = new QPushButton("Обрати процес", this);
  process_button->setToolTip(
      "Підключитись до вже запущеної програми. Без root на Ubuntu "
      "(ptrace_scope=1) часто блокується — тоді краще «Запустити» або "
      "«З правами root».");
  auto *launch_button = new QPushButton("Запустити", this);
  launch_button->setToolTip(
      "Стартувати гру як дочірній процес — основний шлях без root. "
      "На Ubuntu з ptrace_scope=1 це найнадійніше.");
  auto *root_button = new QPushButton("З правами root", this);
  root_button->setToolTip(
      "Запустити ще один GameX з правами root (пароль через zenity/sudo). "
      "Потрібно, щоб чіплятись до вже відкритих ігор при ptrace_scope=1. "
      "Не тримайте root постійно без потреби.");
  root_button->setEnabled(!elevated_privileges());
  auto *region_button = new QPushButton("Блок пам'яті", this);
  region_button->setToolTip(
      "Обмежити пошук одним регіоном або знову шукати по всьому процесу");
  process_label_ = new QLabel("Процес не обрано", this);
  region_label_ = new QLabel("Область: увесь процес", this);

  value_label_ = new QLabel("Що шукати", this);
  value_edit_ = new QLineEdit(this);
  value_edit_->setPlaceholderText("100 · 100*10 · 64h · DE ?? AD");
  value_edit_->setMinimumWidth(160);
  value_edit_->setToolTip("Число з гри, вираз (100*10, 100 xor BAADBAAD) або "
                          "шаблон байтів DE ?? AD.");
  upper_label_ = new QLabel("до", this);
  upper_edit_ = new QLineEdit(this);
  upper_edit_->setPlaceholderText("верхня межа");
  upper_edit_->setVisible(false);
  upper_label_->setVisible(false);

  type_combo_ = new QComboBox(this);
  type_combo_->setToolTip("Як гра зберігає число в пам'яті");
  endian_combo_ = new QComboBox(this);
  endian_combo_->setToolTip(
      "Порядок байтів. Для звичайних Linux-ігор залиште «молодший спочатку».");
  op_combo_ = new QComboBox(this);
  op_combo_->setToolTip("Умова першого пошуку або відсіву");
  alignment_ = new QSpinBox(this);
  alignment_->setToolTip("Крок перебору адрес. Це не значення з гри. Для "
                         "цілого 4 байти зазвичай 4.");
  writable_only_ = new QCheckBox("Лише записувані", this);
  writable_only_->setChecked(true);
  writable_only_->setToolTip("Шукати тільки в пам'яті, куди можна писати");

  add_type(type_combo_, "Ціле 1 байт", ValueType::Int8);
  add_type(type_combo_, "Беззнакове 1 байт", ValueType::UInt8);
  add_type(type_combo_, "Ціле 2 байти", ValueType::Int16);
  add_type(type_combo_, "Беззнакове 2 байти", ValueType::UInt16);
  add_type(type_combo_, "Ціле 3 байти", ValueType::Int24);
  add_type(type_combo_, "Ціле 4 байти", ValueType::Int32);
  add_type(type_combo_, "Беззнакове 4 байти", ValueType::UInt32);
  add_type(type_combo_, "Ціле 8 байтів", ValueType::Int64);
  add_type(type_combo_, "Беззнакове 8 байтів", ValueType::UInt64);
  add_type(type_combo_, "Дріб 4 байти", ValueType::Float32);
  add_type(type_combo_, "Дріб 8 байтів", ValueType::Float64);
  add_type(type_combo_, "Рядок UTF-8", ValueType::StringUtf8);
  add_type(type_combo_, "Байти", ValueType::ByteArray);
  type_combo_->setCurrentIndex(5);

  endian_combo_->addItem("Молодший байт спочатку",
                         static_cast<int>(Endian::Little));
  endian_combo_->addItem("Старший байт спочатку",
                         static_cast<int>(Endian::Big));

  op_combo_->addItem("Дорівнює", static_cast<int>(CompareOp::Equal));
  op_combo_->addItem("Невідоме значення", static_cast<int>(CompareOp::Unknown));
  op_combo_->addItem("Не дорівнює", static_cast<int>(CompareOp::NotEqual));
  op_combo_->addItem("Більше", static_cast<int>(CompareOp::Greater));
  op_combo_->addItem("Більше або дорівнює",
                     static_cast<int>(CompareOp::GreaterOrEqual));
  op_combo_->addItem("Менше", static_cast<int>(CompareOp::Less));
  op_combo_->addItem("Менше або дорівнює",
                     static_cast<int>(CompareOp::LessOrEqual));
  op_combo_->addItem("Діапазон", static_cast<int>(CompareOp::Between));
  op_combo_->addItem("Зросло", static_cast<int>(CompareOp::Increased));
  op_combo_->addItem("Зменшилось", static_cast<int>(CompareOp::Decreased));
  op_combo_->addItem("Змінилось", static_cast<int>(CompareOp::Changed));
  op_combo_->addItem("Не змінилось", static_cast<int>(CompareOp::Unchanged));
  op_combo_->setCurrentIndex(0);

  alignment_->setRange(1, 4096);
  alignment_->setValue(4);
  alignment_->setPrefix("крок ");
  max_region_mib_ = new QSpinBox(this);
  max_region_mib_->setRange(0, 16384);
  max_region_mib_->setValue(512);
  max_region_mib_->setSuffix(" МБ");
  max_region_mib_->setSpecialValueText("без ліміту");
  max_region_mib_->setToolTip("Пропускати регіони більші за цей розмір. Це не "
                              "число з гри. 0 — без обмеження.");

  scan_button_ = new QPushButton("Шукати", this);
  scan_button_->setToolTip("Перший прохід по пам'яті");
  refine_button_ = new QPushButton("Відсіяти", this);
  refine_button_->setToolTip(
      "Залишити лише адреси, які відповідають новій умові");
  stop_button_ = new QPushButton("Стоп", this);
  stop_button_->setEnabled(false);
  reset_button_ = new QPushButton("Скинути пошук", this);
  reset_button_->setToolTip(
      "Очистити список збігів і знімок, щоб знову натиснути «Шукати». "
      "Нижня таблиця адрес лишається.");

  progress_ = new QProgressBar(this);
  progress_->setTextVisible(true);
  progress_->setRange(0, 1000);
  progress_->setValue(0);
  progress_->setFormat("Очікування");

  auto *process_box = new QGroupBox("1. Процес", this);
  auto *process_row = new QHBoxLayout(process_box);
  emulator_combo_ = new QComboBox(this);
  emulator_combo_->setToolTip("Профіль приставки: адреси в таблиці — гостьові, "
                              "база RAM шукається в maps");
  emulator_combo_->addItem("Без емулятора", QString{});
  for (const EmulatorProfile &profile : builtin_emulator_profiles()) {
    emulator_combo_->addItem(QString::fromStdString(profile.title),
                             QString::fromStdString(profile.id));
  }
  auto *emu_detect = new QPushButton("База RAM", this);
  emu_detect->setToolTip("Знайти блок гостьової RAM за обраним профілем");
  emulator_ram_label_ = new QLabel("RAM: —", this);

  process_row->addWidget(process_button);
  process_row->addWidget(launch_button);
  process_row->addWidget(root_button);
  process_row->addWidget(region_button);
  process_row->addWidget(new QLabel("Емулятор", this));
  process_row->addWidget(emulator_combo_);
  process_row->addWidget(emu_detect);
  process_row->addWidget(emulator_ram_label_);
  process_row->addWidget(process_label_, 1);
  process_row->addWidget(region_label_);

  auto *search_box = new QGroupBox("2. Пошук у пам'яті", this);
  auto *search_grid = new QGridLayout(search_box);
  search_grid->addWidget(value_label_, 0, 0);
  search_grid->addWidget(value_edit_, 0, 1);
  search_grid->addWidget(upper_label_, 0, 2);
  search_grid->addWidget(upper_edit_, 0, 3);
  search_grid->addWidget(new QLabel("Тип", this), 0, 4);
  search_grid->addWidget(type_combo_, 0, 5);
  search_grid->addWidget(new QLabel("Умова", this), 0, 6);
  search_grid->addWidget(op_combo_, 0, 7);

  search_grid->addWidget(new QLabel("Порядок байтів", this), 1, 0);
  search_grid->addWidget(endian_combo_, 1, 1);
  search_grid->addWidget(new QLabel("Крок адреси", this), 1, 2);
  search_grid->addWidget(alignment_, 1, 3);
  search_grid->addWidget(new QLabel("Макс. регіон", this), 1, 4);
  search_grid->addWidget(max_region_mib_, 1, 5);
  search_grid->addWidget(writable_only_, 1, 6);

  auto *actions = new QHBoxLayout();
  actions->addWidget(scan_button_);
  actions->addWidget(refine_button_);
  actions->addWidget(stop_button_);
  actions->addWidget(reset_button_);
  actions->addStretch();
  search_grid->addLayout(actions, 2, 0, 1, 8);
  search_grid->setColumnStretch(1, 1);

  results_ = new QTableWidget(this);
  results_->setColumnCount(2);
  results_->setHorizontalHeaderLabels({"Адреса", "Значення зараз"});
  results_->setSelectionBehavior(QAbstractItemView::SelectRows);
  results_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  results_->verticalHeader()->setVisible(false);
  results_->horizontalHeader()->setStretchLastSection(true);
  results_->setToolTip("Це лише знайдені адреси. Щоб змінити число в грі, "
                       "додайте рядок у таблицю нижче.");

  table_ = new GamexTable(this);
  table_->setColumnCount(ColCount);
  table_->setHorizontalHeaderLabels({"Тримати", "Режим", "Група", "Опис",
                                     "Прив'язка", "Адреса", "Тип", "Мін",
                                     "Макс", "Значення → у гру"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setSectionResizeMode(ColDescription,
                                                   QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(ColBinding,
                                                   QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(ColValue,
                                                   QHeaderView::Stretch);
  table_->setToolTip(
      "F5 — тримати · F6 — значення · F7 — режим · F8 — запам'ятати. "
      "Червона адреса = модуль не знайдено після перезапуску.");

  group_filter_ = new QLineEdit(this);
  group_filter_->setPlaceholderText("Фільтр за групою або описом");
  group_filter_->setClearButtonEnabled(true);
  group_filter_->setToolTip("Порожньо — показати всі рядки таблиці");

  auto *add_button = new QPushButton("Додати обране ↓", this);
  add_button->setToolTip(
      "Перенести вибрані збіги вниз, щоб редагувати і заморожувати");
  auto *remove_button = new QPushButton("Прибрати", this);
  auto *bind_button = new QPushButton("До модуля", this);
  bind_button->setToolTip(
      "Зробити прив'язку module+зсув, щоб адреса переживала перезапуск");
  auto *pointer_button = new QPushButton("Вказівник…", this);
  pointer_button->setToolTip(
      "Шукати статичний вказівник на виділену адресу (глибина 1–4)");
  auto *hex_button = new QPushButton("Hex…", this);
  hex_button->setToolTip("Перегляд і запис байтів навколо виділеної адреси");
  auto *structure_button = new QPushButton("Структура…", this);
  structure_button->setToolTip(
      "Шаблон кількох полів зі зсувами; пошук одним проходом");
  auto *watch_button = new QPushButton("Хто пише…", this);
  watch_button->setToolTip(
      "Апаратний watchpoint (ptrace). Лише процес з GameX. RIP у звіті — "
      "інструкція після запису; нові потоки підхоплюються (TRACECLONE).");
  auto *save_button = new QPushButton("Зберегти .gxt", this);
  auto *load_button = new QPushButton("Завантажити .gxt", this);

  for (int i = 0; i < kDumpSlots; ++i) {
    dump_buttons_[static_cast<std::size_t>(i)] =
        new QPushButton(QString("M%1").arg(i + 1), this);
    dump_buttons_[static_cast<std::size_t>(i)]->setMinimumWidth(44);
    dump_buttons_[static_cast<std::size_t>(i)]->setToolTip(
        QString("Зберегти поточні значення збігів як знімок M%1 "
                "(після пошуку / відсіву)")
            .arg(i + 1));
  }
  formula_preset_ = new QComboBox(this);
  formula_preset_->setToolTip("Підставить готову формулу в поле праворуч");
  formula_preset_->addItem("Шаблон формули…", QString());
  formula_preset_->addItem("Змінилось між M1 і M2", QStringLiteral("M1<>M2"));
  formula_preset_->addItem("Змінилось, потім стабільно (M2=M3)",
                           QStringLiteral("(M1<>M2)and(M2=M3)"));
  formula_preset_->addItem("Змінилось і повернулось до M1",
                           QStringLiteral("(M1<>M2)and(M1=M3)"));
  formula_preset_->addItem("У M2 більше ніж у M1", QStringLiteral("M2>M1"));
  formula_preset_->addItem("У M2 менше ніж у M1", QStringLiteral("M2<M1"));
  formula_edit_ = new QLineEdit(this);
  formula_edit_->setPlaceholderText("M1<>M2  або  (M1<>M2)and(M2=M3)");
  formula_edit_->setToolTip(
      "Умова по збережених знімках M1…M4. Спочатку збережіть M "
      "(кнопки зліва), змініть гру, збережіть наступний M, потім "
      "«Відсіяти формулою». Довідка — кнопка «?».");
  auto *formula_button = new QPushButton("Відсіяти формулою", this);
  formula_button->setToolTip(
      "Залишити в списку збігів лише адреси, де формула істинна");
  auto *dump_help_button = new QPushButton("?", this);
  dump_help_button->setFixedWidth(28);
  dump_help_button->setToolTip("Довідка: знімки M1–M4 і формули");

  hits_hint_ =
      new QLabel("Знайдені адреси. Подвійне клацання або кнопка «Додати обране "
                 "↓» переносить їх у таблицю редагування.",
                 this);
  hits_hint_->setWordWrap(true);
  table_hint_ = new QLabel("3. Запис: «Значення → у гру» + Enter. «До модуля» "
                           "/ «Вказівник…» — стійкість "
                           "до ASLR. Профіль емулятора — гостьові адреси "
                           "приставки. F5–F8 · F1 — довідка.",
                           this);
  table_hint_->setWordWrap(true);

  auto *top_box = new QWidget(this);
  auto *top_layout = new QVBoxLayout(top_box);
  top_layout->setContentsMargins(0, 0, 0, 0);
  top_layout->addWidget(hits_hint_);
  top_layout->addWidget(results_);
  top_layout->addWidget(add_button);

  auto *bottom_box = new QWidget(this);
  auto *bottom_layout = new QVBoxLayout(bottom_box);
  bottom_layout->setContentsMargins(0, 0, 0, 0);
  bottom_layout->addWidget(table_hint_);
  bottom_layout->addWidget(group_filter_);
  bottom_layout->addWidget(table_);
  auto *table_buttons = new QHBoxLayout();
  auto *dump_box = new QGroupBox(
      "Знімки M1–M4 — порівняти кілька станів однією формулою", this);
  dump_box->setToolTip(
      "Не плутати з звичайним «Відсіяти». Тут ви самі зберігаєте "
      "значення в M1…M4 у різні моменти, потім відсіваєте формулою.");
  auto *dump_layout = new QVBoxLayout(dump_box);
  auto *dump_hint = new QLabel(
      "1) Після пошуку натисніть M1 → 2) змініть число в грі → 3) M2 "
      "(за потреби M3/M4) → 4) оберіть шаблон або впишіть формулу → "
      "5) «Відсіяти формулою». Звичайне «Відсіяти» зверху — для "
      "простіших кроків.",
      dump_box);
  dump_hint->setWordWrap(true);
  dump_hint->setStyleSheet("color: #666;");
  auto *dump_row = new QHBoxLayout();
  dump_row->addWidget(new QLabel("Зберегти:", dump_box));
  for (QPushButton *button : dump_buttons_) {
    dump_row->addWidget(button);
  }
  dump_row->addSpacing(8);
  dump_row->addWidget(formula_preset_);
  dump_row->addWidget(formula_edit_, 1);
  dump_row->addWidget(formula_button);
  dump_row->addWidget(dump_help_button);
  dump_layout->addWidget(dump_hint);
  dump_layout->addLayout(dump_row);
  bottom_layout->addWidget(dump_box);

  table_buttons->addWidget(remove_button);
  table_buttons->addWidget(bind_button);
  table_buttons->addWidget(pointer_button);
  table_buttons->addWidget(hex_button);
  table_buttons->addWidget(structure_button);
  table_buttons->addWidget(watch_button);
  table_buttons->addWidget(save_button);
  table_buttons->addWidget(load_button);
  table_buttons->addStretch();
  bottom_layout->addLayout(table_buttons);

  auto *splitter = new QSplitter(Qt::Vertical, this);
  splitter->addWidget(top_box);
  splitter->addWidget(bottom_box);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);

  auto *central = new QWidget(this);
  auto *central_layout = new QVBoxLayout(central);
  central_layout->setContentsMargins(8, 8, 8, 8);
  central_layout->addWidget(process_box);
  central_layout->addWidget(search_box);
  central_layout->addWidget(progress_);
  central_layout->addWidget(splitter, 1);
  setCentralWidget(central);
  if (elevated_privileges()) {
    statusBar()->showMessage(
        "Режим root. Оберіть процес (і вже відкриті ігри теж). "
        "Не тримайте підвищені права без потреби.");
  } else {
    statusBar()->showMessage("Спочатку оберіть процес. Потім знайдіть число → "
                             "додайте вниз → змініть значення.");
  }

  build_menus();

  connect(process_button, &QPushButton::clicked, this,
          &MainWindow::choose_process);
  connect(emu_detect, &QPushButton::clicked, this,
          &MainWindow::detect_emulator_ram);
  connect(emulator_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, [this](int) { on_emulator_changed(); });
  connect(launch_button, &QPushButton::clicked, this,
          &MainWindow::launch_process);
  connect(root_button, &QPushButton::clicked, this,
          &MainWindow::restart_as_root);
  connect(region_button, &QPushButton::clicked, this,
          &MainWindow::choose_region);
  connect(scan_button_, &QPushButton::clicked, this,
          [this] { run_scan(false); });
  connect(refine_button_, &QPushButton::clicked, this,
          [this] { run_scan(true); });
  connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stop_scan);
  connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_scan);
  connect(add_button, &QPushButton::clicked, this,
          &MainWindow::add_selected_hit);
  connect(remove_button, &QPushButton::clicked, this,
          &MainWindow::remove_selected_entries);
  connect(bind_button, &QPushButton::clicked, this,
          &MainWindow::bind_selected_to_module);
  connect(pointer_button, &QPushButton::clicked, this,
          &MainWindow::find_pointers_for_selected);
  connect(hex_button, &QPushButton::clicked, this,
          &MainWindow::open_hex_for_selected);
  connect(structure_button, &QPushButton::clicked, this,
          &MainWindow::open_structure_dialog);
  connect(watch_button, &QPushButton::clicked, this,
          &MainWindow::open_watch_for_selected);
  for (int i = 0; i < kDumpSlots; ++i) {
    connect(dump_buttons_[static_cast<std::size_t>(i)], &QPushButton::clicked,
            this, [this, i] { capture_dump_slot(i); });
  }
  connect(formula_button, &QPushButton::clicked, this,
          &MainWindow::refine_by_formula);
  connect(dump_help_button, &QPushButton::clicked, this,
          &MainWindow::open_dump_help);
  connect(formula_preset_, qOverload<int>(&QComboBox::currentIndexChanged),
          this, [this](int) {
            if (formula_preset_ == nullptr || formula_edit_ == nullptr) {
              return;
            }
            const QString text = formula_preset_->currentData().toString();
            if (!text.isEmpty()) {
              formula_edit_->setText(text);
            }
          });
  connect(save_button, &QPushButton::clicked, this, &MainWindow::save_table);
  connect(load_button, &QPushButton::clicked, this, &MainWindow::load_table);
  connect(op_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { update_value_fields(); });
  connect(type_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    const auto type =
        static_cast<ValueType>(type_combo_->currentData().toInt());
    const std::size_t width = fixed_width(type);
    alignment_->setValue(width == 0 ? 1 : static_cast<int>(width));
  });
  connect(table_, &QTableWidget::itemChanged, this,
          &MainWindow::on_table_changed);
  connect(results_, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *item) {
            if (item != nullptr) {
              results_->selectRow(item->row());
            }
            add_selected_hit();
          });
  connect(group_filter_, &QLineEdit::textChanged, this,
          [this](const QString &) { apply_group_filter(); });
  update_value_fields();
  update_dump_indicators();
  update_scan_controls();
}

void MainWindow::build_menus() {
  auto *file_menu = menuBar()->addMenu("Файл");
  auto *save_action = file_menu->addAction("Зберегти таблицю…");
  save_action->setShortcut(QKeySequence::Save);
  connect(save_action, &QAction::triggered, this, &MainWindow::save_table);
  auto *load_action = file_menu->addAction("Завантажити таблицю…");
  load_action->setShortcut(QKeySequence::Open);
  connect(load_action, &QAction::triggered, this, &MainWindow::load_table);
  file_menu->addSeparator();
  auto *quit_action = file_menu->addAction("Вийти");
  quit_action->setShortcut(QKeySequence::Quit);
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  auto *process_menu = menuBar()->addMenu("Процес");
  auto *choose_action = process_menu->addAction("Обрати процес…");
  connect(choose_action, &QAction::triggered, this,
          &MainWindow::choose_process);
  auto *launch_action = process_menu->addAction("Запустити…");
  connect(launch_action, &QAction::triggered, this,
          &MainWindow::launch_process);
  auto *root_action = process_menu->addAction("Перезапустити з правами root…");
  root_action->setEnabled(!elevated_privileges());
  connect(root_action, &QAction::triggered, this, &MainWindow::restart_as_root);
  auto *region_action = process_menu->addAction("Блок пам'яті…");
  connect(region_action, &QAction::triggered, this, &MainWindow::choose_region);
  auto *ram_action = process_menu->addAction("База RAM емулятора…");
  connect(ram_action, &QAction::triggered, this,
          &MainWindow::detect_emulator_ram);
  process_menu->addSeparator();
  auto *disconnect_action = process_menu->addAction("Від'єднати процес");
  connect(disconnect_action, &QAction::triggered, this,
          &MainWindow::disconnect_process);

  auto *search_menu = menuBar()->addMenu("Пошук");
  scan_action_ = search_menu->addAction("Шукати");
  connect(scan_action_, &QAction::triggered, this, [this] { run_scan(false); });
  refine_action_ = search_menu->addAction("Відсіяти");
  connect(refine_action_, &QAction::triggered, this,
          [this] { run_scan(true); });
  stop_action_ = search_menu->addAction("Стоп");
  connect(stop_action_, &QAction::triggered, this, &MainWindow::stop_scan);
  reset_action_ = search_menu->addAction("Скинути пошук");
  connect(reset_action_, &QAction::triggered, this, &MainWindow::reset_scan);
  search_menu->addSeparator();
  for (int i = 0; i < kDumpSlots; ++i) {
    auto *dump_action =
        search_menu->addAction(QString("Зберегти знімок M%1").arg(i + 1));
    connect(dump_action, &QAction::triggered, this,
            [this, i] { capture_dump_slot(i); });
  }
  auto *formula_action = search_menu->addAction("Відсіяти за формулою…");
  connect(formula_action, &QAction::triggered, this,
          &MainWindow::refine_by_formula);
  search_menu->addSeparator();
  auto *add_action = search_menu->addAction("Додати обране в таблицю");
  connect(add_action, &QAction::triggered, this, &MainWindow::add_selected_hit);

  auto *table_menu = menuBar()->addMenu("Таблиця");
  auto *remove_action = table_menu->addAction("Прибрати виділене");
  remove_action->setShortcut(QKeySequence::Delete);
  connect(remove_action, &QAction::triggered, this,
          &MainWindow::remove_selected_entries);
  auto *bind_action = table_menu->addAction("Прив'язати до модуля");
  connect(bind_action, &QAction::triggered, this,
          &MainWindow::bind_selected_to_module);
  auto *pointer_action = table_menu->addAction("Шукати вказівник…");
  connect(pointer_action, &QAction::triggered, this,
          &MainWindow::find_pointers_for_selected);
  table_menu->addSeparator();
  auto *hold_action = table_menu->addAction("Перемкнути «Тримати»");
  hold_action->setShortcut(QKeySequence(Qt::Key_F5));
  connect(hold_action, &QAction::triggered, this,
          &MainWindow::toggle_freeze_selected);
  auto *value_action = table_menu->addAction("Задати значення…");
  value_action->setShortcut(QKeySequence(Qt::Key_F6));
  connect(value_action, &QAction::triggered, this,
          &MainWindow::set_value_selected);
  auto *mode_action = table_menu->addAction("Цикл режиму утримання");
  mode_action->setShortcut(QKeySequence(Qt::Key_F7));
  connect(mode_action, &QAction::triggered, this,
          &MainWindow::cycle_freeze_mode_selected);
  auto *capture_action = table_menu->addAction("Запам'ятати поточне значення");
  capture_action->setShortcut(QKeySequence(Qt::Key_F8));
  connect(capture_action, &QAction::triggered, this,
          &MainWindow::capture_value_selected);

  auto *tools_menu = menuBar()->addMenu("Інструменти");
  auto *hex_action = tools_menu->addAction("Hex-перегляд…");
  connect(hex_action, &QAction::triggered, this,
          &MainWindow::open_hex_for_selected);
  auto *structure_action = tools_menu->addAction("Пошук структури…");
  connect(structure_action, &QAction::triggered, this,
          &MainWindow::open_structure_dialog);
  auto *watch_action = tools_menu->addAction("Хто пише…");
  connect(watch_action, &QAction::triggered, this,
          &MainWindow::open_watch_for_selected);

  auto *help_menu = menuBar()->addMenu("Довідка");
  auto *help_action = help_menu->addAction("Як користуватись…");
  help_action->setShortcut(QKeySequence::HelpContents);
  connect(help_action, &QAction::triggered, this, &MainWindow::open_help);
  help_menu->addSeparator();
  auto *about_action = help_menu->addAction("Про програму…");
  connect(about_action, &QAction::triggered, this, &MainWindow::open_about);
  auto *about_qt_action = help_menu->addAction("Про Qt…");
  connect(about_qt_action, &QAction::triggered, this,
          &MainWindow::open_about_qt);
}

void MainWindow::choose_process() {
  ProcessDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  attach_pid(dialog.selected_pid(),
             QString("pid %1").arg(dialog.selected_pid()));
}

void MainWindow::restart_as_root() {
  if (elevated_privileges()) {
    QMessageBox::information(this, "GameX",
                             "GameX уже запущено з правами root.");
    return;
  }
  if (scanning_) {
    QMessageBox::information(this, "GameX", "Спочатку зупиніть пошук (Стоп).");
    return;
  }
  if (elevate_process_ != nullptr &&
      elevate_process_->state() != QProcess::NotRunning) {
    QMessageBox::information(this, "GameX",
                             "Уже триває запуск з root — дочекайтесь вікна "
                             "або завершення попередньої спроби.");
    return;
  }

  const QString app_path = QCoreApplication::applicationFilePath();
  const QString sudo_hint = QString("sudo -E \"%1\"").arg(app_path);

  QMessageBox confirm(this);
  confirm.setIcon(QMessageBox::Question);
  confirm.setWindowTitle("GameX — права root");
  confirm.setText("Запустити ще один GameX з правами root?");
  confirm.setInformativeText(
      "Далі з'явиться вікно пароля.\n"
      "Нове вікно «GameX (root)» відкриється окремо.\n\n"
      "Потрібно, щоб підключатись до вже запущених ігор "
      "(коли «Обрати процес» без root не читає пам'ять).\n"
      "Не тримайте root довше, ніж потрібно.\n\n"
      "Запасний варіант у терміналі:\n" +
      sudo_hint);
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  confirm.setDefaultButton(QMessageBox::No);
  if (confirm.exec() != QMessageBox::Yes) {
    return;
  }

  if (!qgetenv("DISPLAY").isEmpty()) {
    const QString xhost = QStandardPaths::findExecutable("xhost");
    if (!xhost.isEmpty()) {
      QProcess::execute(xhost, {QStringLiteral("+SI:localuser:root")});
    }
  }

  const QString sudo = QStandardPaths::findExecutable("sudo");
  const QString zenity = QStandardPaths::findExecutable("zenity");
  if (sudo.isEmpty() || zenity.isEmpty()) {
    QMessageBox::warning(
        this, "GameX",
        QString("Потрібні sudo і zenity.\nЗапустіть у терміналі:\n\n%1")
            .arg(sudo_hint));
    return;
  }

  // Пароль через zenity → sudo -S (без askpass-файла: він давав ETXTBSY).
  // pkexec зрізає DISPLAY, тож ним не користуємось.
  write_status("Введіть пароль у вікні…");
  QProcess password_prompt;
  password_prompt.start(zenity,
                        {QStringLiteral("--password"),
                         QStringLiteral("--title=GameX — пароль root")});
  if (!password_prompt.waitForFinished(-1)) {
    QMessageBox::warning(this, "GameX",
                         "Не вдалося відкрити вікно пароля (zenity).\n\n" +
                             sudo_hint);
    return;
  }
  if (password_prompt.exitCode() != 0) {
    write_status("Запуск з root скасовано");
    return;
  }
  QByteArray password = password_prompt.readAllStandardOutput();
  while (!password.isEmpty() &&
         (password.endsWith('\n') || password.endsWith('\r'))) {
    password.chop(1);
  }
  if (password.isEmpty()) {
    write_status("Порожній пароль — скасовано");
    return;
  }

  if (elevate_process_ != nullptr) {
    elevate_process_->deleteLater();
    elevate_process_ = nullptr;
  }
  elevate_process_ = new QProcess(this);
  elevate_process_->setProcessChannelMode(QProcess::MergedChannels);
  elevate_process_->setProcessEnvironment(
      QProcessEnvironment::systemEnvironment());

  connect(elevate_process_,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, sudo_hint](int code, QProcess::ExitStatus) {
            const QString out =
                QString::fromLocal8Bit(elevate_process_->readAll()).trimmed();
            if (code != 0) {
              QString message =
                  QString("Не вдалося запустити з root (код %1).").arg(code);
              if (!out.isEmpty()) {
                message += "\n\n" + out;
              }
              message += "\n\nСпробуйте в терміналі:\n" + sudo_hint;
              QMessageBox::warning(this, "GameX", message);
              write_status("Запуск з root не вдався");
            } else {
              write_status("Сеанс GameX (root) завершено");
            }
            if (elevate_process_ != nullptr) {
              elevate_process_->deleteLater();
              elevate_process_ = nullptr;
            }
          });
  connect(elevate_process_, &QProcess::errorOccurred, this,
          [this, sudo_hint](QProcess::ProcessError) {
            if (elevate_process_ == nullptr) {
              return;
            }
            QMessageBox::warning(
                this, "GameX",
                QString("Помилка запуску sudo: %1\n\n%2")
                    .arg(elevate_process_->errorString(), sudo_hint));
            write_status("Запуск з root не вдався");
            elevate_process_->deleteLater();
            elevate_process_ = nullptr;
          });

  const QStringList args = {
      QStringLiteral("-S"),
      QStringLiteral(
          "--preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR,"
          "QT_QPA_PLATFORM,XDG_SESSION_TYPE"),
      app_path,
  };
  elevate_process_->start(sudo, args);
  if (!elevate_process_->waitForStarted(5000)) {
    QMessageBox::warning(this, "GameX",
                         QString("sudo не стартував: %1\n\n%2")
                             .arg(elevate_process_->errorString(), sudo_hint));
    elevate_process_->deleteLater();
    elevate_process_ = nullptr;
    password.fill('\0');
    return;
  }
  elevate_process_->write(password);
  elevate_process_->write("\n");
  elevate_process_->closeWriteChannel();
  password.fill('\0');

  write_status("Запуск GameX (root)…");
}

void MainWindow::launch_process() {
  bool ok = false;
  const QString command =
      QInputDialog::getText(this, "Запустити процес",
                            "Команда без оболонки. GameX стане батьківським "
                            "процесом, тож читання пам'яті дозволене.",
                            QLineEdit::Normal, {}, &ok);
  if (!ok || command.trimmed().isEmpty()) {
    return;
  }
  const QStringList parts = QProcess::splitCommand(command);
  if (parts.isEmpty()) {
    QMessageBox::warning(this, "GameX", "Порожня команда");
    return;
  }
  if (child_ == nullptr) {
    child_ = new QProcess(this);
    connect(child_, &QProcess::started, this, [this] {
      launched_pid_ = static_cast<pid_t>(child_->processId());
      attach_pid(launched_pid_, child_->program());
    });
    connect(child_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
              if (ignore_child_errors_ || error != QProcess::FailedToStart) {
                return;
              }
              QMessageBox::warning(this, "GameX", child_->errorString());
            });
    connect(child_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
              if (ignore_child_errors_) {
                return;
              }
              if (memory_ && launched_pid_ > 0 &&
                  memory_->pid() == launched_pid_) {
                detach_process("Процес завершився, підключення скинуто");
              }
            });
  } else if (child_->state() != QProcess::NotRunning) {
    const pid_t previous = launched_pid_;
    ignore_child_errors_ = true;
    child_->kill();
    child_->waitForFinished(1000);
    ignore_child_errors_ = false;
    if (memory_ && previous > 0 && memory_->pid() == previous) {
      detach_process("Попередній процес зупинено");
    }
  }
  child_->setProgram(parts.front());
  child_->setArguments(parts.mid(1));
  child_->start();
}

void MainWindow::attach_pid(pid_t pid, const QString &title) {
  if (scanning_) {
    QMessageBox::information(this, "GameX", "Спочатку зупиніть пошук");
    return;
  }
  std::string error;
  auto opened = ProcessMemory::open(pid, &error);
  if (!opened) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  wait_for_scan_thread();
  scan_session_ = false;
  scanner_.reset();
  memory_ = std::make_unique<ProcessMemory>(std::move(*opened));

  std::string probe_error;
  if (!memory_->probe_read(&probe_error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(probe_error));
    memory_.reset();
    process_label_->setText("Процес не обрано");
    update_scan_controls();
    return;
  }

  scanner_ = std::make_unique<Scanner>(*memory_);
  dumps_.clear();
  update_dump_indicators();
  limit_region_ = false;
  region_label_->setText("Область: увесь процес");
  process_label_->setText(title);
  results_->setRowCount(0);

  const QString scope = QString::fromStdString(ptrace_scope());
  const bool as_root = elevated_privileges();
  QString access = "Процес підключено · читання пам'яті OK.";
  if (as_root) {
    access += " Режим root (підвищені права) — можна чіплятись і до "
              "не-дочірніх процесів.";
  } else if (scope == "1") {
    access += " ptrace_scope=1: без root надійніше запускати гру через "
              "«Запустити».";
  } else if (scope == "0") {
    access += " ptrace_scope=0: читання процесів того ж користувача "
              "зазвичай дозволене.";
  } else if (!scope.isEmpty()) {
    access += QString(" ptrace_scope=%1.").arg(scope);
  }
  const std::size_t failed = resolve_addresses();
  if (failed == 0) {
    access += " Прив'язки оновлено.";
  } else {
    access += QString(" Не розв'язано %1 прив'язок (червоні рядки).")
                  .arg(static_cast<qulonglong>(failed));
  }
  statusBar()->showMessage(access);
  update_scan_controls();
}

ScanQuery MainWindow::current_query(std::string *error) const {
  ScanQuery query;
  query.type = static_cast<ValueType>(type_combo_->currentData().toInt());
  query.endian = static_cast<Endian>(endian_combo_->currentData().toInt());
  query.op = static_cast<CompareOp>(op_combo_->currentData().toInt());
  query.alignment = static_cast<std::size_t>(alignment_->value());
  query.writable_only = writable_only_->isChecked();
  const int mib = max_region_mib_->value();
  query.max_region_bytes =
      mib <= 0 ? 0 : static_cast<std::uint64_t>(mib) * 1024ull * 1024ull;
  if (limit_region_) {
    query.region_start = region_start_;
    query.region_end = region_end_;
  }
  query.skip_path = executable_path(static_cast<pid_t>(::getpid()));
  const bool relative =
      query.op == CompareOp::Increased || query.op == CompareOp::Decreased ||
      query.op == CompareOp::Changed || query.op == CompareOp::Unchanged ||
      query.op == CompareOp::Unknown;
  if (!relative) {
    auto prepared = prepare_search_value(
        query.type, query.endian, value_edit_->text().toStdString(), error);
    if (!prepared) {
      return query;
    }
    query.value = std::move(prepared->value);
    query.mask = std::move(prepared->mask);
    if (query.op == CompareOp::Between) {
      auto upper = prepare_search_value(
          query.type, query.endian, upper_edit_->text().toStdString(), error);
      if (!upper) {
        return query;
      }
      query.upper = std::move(upper->value);
    }
  }
  if (error) {
    error->clear();
  }
  return query;
}

void MainWindow::start_scan_worker(ScanQuery query, bool refine,
                                   std::optional<StructureTemplate> structure) {
  cancel_.store(false);
  set_scanning(true);
  progress_->setValue(0);
  if (structure) {
    progress_->setFormat("Структура…");
  } else {
    progress_->setFormat(refine ? "Відсів…" : "Пошук…");
  }

  auto *thread = new QThread(this);
  scan_thread_ = thread;
  auto *worker =
      new ScanWorker(scanner_.get(), std::move(query), refine, &cancel_,
                     &last_status_, &status_mutex_, std::move(structure));
  worker->moveToThread(thread);
  connect(thread, &QThread::started, worker, &ScanWorker::start);
  connect(worker, &ScanWorker::progress, this, &MainWindow::on_scan_progress);
  connect(worker, &ScanWorker::completed, this, &MainWindow::on_scan_completed);
  connect(worker, &ScanWorker::completed, worker, &QObject::deleteLater);
  connect(worker, &ScanWorker::completed, thread, &QThread::quit);
  connect(thread, &QThread::finished, this, [this, thread] {
    if (scan_thread_ == thread) {
      scan_thread_ = nullptr;
    }
  });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

void MainWindow::wait_for_scan_thread() {
  cancel_.store(true);
  if (scan_thread_ != nullptr && scan_thread_->isRunning()) {
    scan_thread_->wait();
  }
  scan_thread_ = nullptr;
  scanning_ = false;
  if (scan_button_ != nullptr) {
    set_scanning(false);
  }
}

void MainWindow::run_scan(bool refine) {
  if (scanning_) {
    return;
  }
  if (!scanner_ || !memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  if (refine) {
    if (!can_refine_scan()) {
      QMessageBox::information(
          this, "GameX",
          last_structure_
              ? "Після пошуку структури спочатку скиньте пошук."
              : "Спочатку виконайте «Шукати», потім уточнюйте «Відсіяти».");
      return;
    }
  } else if (scan_session_) {
    QMessageBox::information(
        this, "GameX",
        "Уже є активний пошук. Натисніть «Скинути пошук», щоб почати знову, "
        "або «Відсіяти», щоб звузити список.");
    return;
  }
  std::string error;
  ScanQuery query = current_query(&error);
  if (!error.empty()) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  last_structure_.reset();
  last_scan_type_ = query.type;
  last_scan_endian_ = query.endian;
  start_scan_worker(std::move(query), refine, std::nullopt);
}

void MainWindow::reset_scan() {
  if (scanning_) {
    stop_scan();
    return;
  }
  if (scanner_) {
    scanner_->reset();
  }
  last_structure_.reset();
  scan_session_ = false;
  dumps_.clear();
  update_dump_indicators();
  results_->setRowCount(0);
  results_->setHorizontalHeaderLabels({"Адреса", "Значення зараз"});
  statusBar()->showMessage("Пошук скинуто — можна знову натиснути «Шукати»");
  update_scan_controls();
}

QString MainWindow::format_hit_value(const ScanHit &hit) const {
  if (!memory_) {
    return {};
  }
  if (last_structure_) {
    std::vector<std::byte> bytes(hit.previous.size());
    const auto io = memory_->read(hit.address, bytes);
    const std::byte *data =
        io.transferred == bytes.size() ? bytes.data() : hit.previous.data();
    const std::size_t available =
        io.transferred == bytes.size() ? bytes.size() : hit.previous.size();
    QStringList parts;
    for (const StructureField &field : last_structure_->fields) {
      if (field.offset + fixed_width(field.type) > available) {
        continue;
      }
      const std::string text =
          decode_text(field.type, last_structure_->endian,
                      std::span<const std::byte>(data + field.offset,
                                                 fixed_width(field.type)));
      const QString name =
          field.description.empty()
              ? QString("+%1").arg(static_cast<qulonglong>(field.offset))
              : QString::fromStdString(field.description);
      parts << QString("%1=%2").arg(name, QString::fromStdString(text));
      if (parts.size() >= 4) {
        break;
      }
    }
    if (parts.isEmpty()) {
      return QString("структура «%1» · %2 Б")
          .arg(QString::fromStdString(last_structure_->name))
          .arg(static_cast<qulonglong>(last_structure_->span()));
    }
    QString text = parts.join(", ");
    if (last_structure_->fields.size() >
        static_cast<std::size_t>(parts.size())) {
      text += "…";
    }
    return text;
  }
  if (!is_fixed_width(last_scan_type_)) {
    return QString::fromStdString(
        decode_text(last_scan_type_, last_scan_endian_, hit.previous));
  }
  std::vector<std::byte> bytes(fixed_width(last_scan_type_));
  const auto io = memory_->read(hit.address, bytes);
  if (io.transferred == bytes.size()) {
    return QString::fromStdString(
        decode_text(last_scan_type_, last_scan_endian_, bytes));
  }
  if (hit.previous.size() == bytes.size()) {
    return QString::fromStdString(
        decode_text(last_scan_type_, last_scan_endian_, hit.previous));
  }
  return "?";
}

void MainWindow::show_hits() {
  if (!scanner_ || !memory_) {
    return;
  }
  constexpr int kPreview = 5000;
  const int rows = static_cast<int>(std::min<std::size_t>(
      scanner_->hit_count(), static_cast<std::size_t>(kPreview)));
  results_->setRowCount(rows);
  const QString value_header =
      last_structure_ ? QString("Поля структури") : QString("Значення зараз");
  results_->setHorizontalHeaderLabels({"Адреса", value_header});
  for (int row = 0; row < rows; ++row) {
    const ScanHit hit = scanner_->hit_at(static_cast<std::size_t>(row));
    auto *address = new QTableWidgetItem(hex_address(hit.address));
    address->setData(Qt::UserRole, static_cast<qulonglong>(hit.address));
    results_->setItem(row, 0, address);
    results_->setItem(row, 1, new QTableWidgetItem(format_hit_value(hit)));
  }
}

void MainWindow::add_selected_hit() {
  if (!memory_) {
    return;
  }
  const auto ranges = results_->selectedRanges();
  if (ranges.isEmpty()) {
    QMessageBox::information(
        this, "GameX",
        "Спочатку виділіть рядок у списку знайдених адрес зверху.");
    return;
  }
  const ModuleMap modules = ModuleMap::from_process(*memory_);
  const EmulatorProfile *emu = current_emulator();
  int added = 0;
  for (const QTableWidgetSelectionRange &range : ranges) {
    for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
      auto *item = results_->item(row, 0);
      if (item == nullptr) {
        continue;
      }
      const auto base =
          static_cast<std::uintptr_t>(item->data(Qt::UserRole).toULongLong());
      if (last_structure_) {
        for (const StructureField &field : last_structure_->fields) {
          AddressEntry entry;
          entry.address = base + field.offset;
          entry.type = field.type;
          entry.endian = last_structure_->endian;
          entry.description =
              last_structure_->name.empty()
                  ? field.description
                  : last_structure_->name + "." + field.description;
          entry.group = last_structure_->name;
          entry.frozen_value = field.value.bytes;
          std::string error;
          if (emu != nullptr) {
            if (!bind_entry_to_emulator(entry, entry.address, &error)) {
              QMessageBox::warning(this, "GameX",
                                   QString::fromStdString(error));
              return;
            }
          } else if (bind_to_module(entry, modules)) {
            entry.description += " [" + entry.module + "]";
          }
          addresses_.add(std::move(entry));
          ++added;
        }
        continue;
      }
      AddressEntry entry;
      entry.address = base;
      entry.type = last_scan_type_;
      entry.endian = last_scan_endian_;
      entry.description =
          "Адреса " + std::to_string(addresses_.entries().size() + 1);
      const std::size_t width =
          is_fixed_width(last_scan_type_) ? fixed_width(last_scan_type_) : 0;
      if (width != 0) {
        entry.frozen_value.resize(width);
        memory_->read(entry.address, entry.frozen_value);
      }
      std::string error;
      if (emu != nullptr) {
        if (!bind_entry_to_emulator(entry, entry.address, &error)) {
          QMessageBox::warning(this, "GameX", QString::fromStdString(error));
          return;
        }
      } else if (bind_to_module(entry, modules)) {
        entry.description += " [" + entry.module + "]";
      }
      addresses_.add(std::move(entry));
      ++added;
    }
  }
  show_entries();
  write_status(
      QString("Додано %1. Якщо є «Прив'язка» — вона пережиє перезапуск.")
          .arg(added));
}

void MainWindow::install_mode_combo(int row, FreezeMode mode) {
  auto label = [](FreezeMode value) {
    const auto text = freeze_mode_label(value);
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
  };
  auto *combo = new QComboBox(table_);
  combo->addItem(label(FreezeMode::Normal),
                 static_cast<int>(FreezeMode::Normal));
  combo->addItem(label(FreezeMode::AllowIncrease),
                 static_cast<int>(FreezeMode::AllowIncrease));
  combo->addItem(label(FreezeMode::AllowDecrease),
                 static_cast<int>(FreezeMode::AllowDecrease));
  combo->addItem(label(FreezeMode::Clamp), static_cast<int>(FreezeMode::Clamp));
  const int index = combo->findData(static_cast<int>(mode));
  combo->setCurrentIndex(index >= 0 ? index : 0);
  combo->setToolTip("Як тримати значення після увімкнення галочки «Тримати»");
  table_->setCellWidget(row, ColMode, combo);
  connect(combo, &QComboBox::currentIndexChanged, this, [this, row](int) {
    if (updating_ || row < 0 ||
        static_cast<std::size_t>(row) >= addresses_.entries().size()) {
      return;
    }
    auto *widget = qobject_cast<QComboBox *>(table_->cellWidget(row, ColMode));
    if (widget == nullptr) {
      return;
    }
    addresses_.entries()[static_cast<std::size_t>(row)].freeze_mode =
        static_cast<FreezeMode>(widget->currentData().toInt());
  });
}

void MainWindow::show_entries() {
  updating_ = true;
  table_->blockSignals(true);
  table_->setRowCount(static_cast<int>(addresses_.entries().size()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    const AddressEntry &entry =
        addresses_.entries()[static_cast<std::size_t>(row)];
    auto *freeze = new QTableWidgetItem();
    freeze->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled |
                     Qt::ItemIsSelectable);
    freeze->setCheckState(entry.frozen ? Qt::Checked : Qt::Unchecked);
    table_->setItem(row, ColHold, freeze);
    install_mode_combo(row, entry.freeze_mode);
    table_->setItem(row, ColGroup,
                    new QTableWidgetItem(QString::fromStdString(entry.group)));
    table_->setItem(
        row, ColDescription,
        new QTableWidgetItem(QString::fromStdString(entry.description)));
    auto *binding =
        new QTableWidgetItem(QString::fromStdString(entry.binding_text()));
    binding->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table_->setItem(row, ColBinding, binding);
    auto *address = new QTableWidgetItem(hex_address(entry.address));
    address->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!entry.resolved) {
      address->setForeground(QBrush(QColor(220, 60, 60)));
      binding->setForeground(QBrush(QColor(220, 60, 60)));
      address->setToolTip("Модуль не знайдено — запис і заморозка вимкнені");
    }
    table_->setItem(row, ColAddress, address);
    auto *type = new QTableWidgetItem(QString::fromUtf8(
        value_type_name(entry.type).data(),
        static_cast<int>(value_type_name(entry.type).size())));
    type->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table_->setItem(row, ColType, type);
    table_->setItem(
        row, ColClampMin,
        new QTableWidgetItem(QString::fromStdString(entry.clamp_min)));
    table_->setItem(
        row, ColClampMax,
        new QTableWidgetItem(QString::fromStdString(entry.clamp_max)));
    QString value_text;
    if (entry.resolved && memory_ && memory_->alive() &&
        is_fixed_width(entry.type)) {
      std::vector<std::byte> bytes(fixed_width(entry.type));
      const auto io = memory_->read(entry.address, bytes);
      if (io.transferred == bytes.size()) {
        value_text = QString::fromStdString(
            decode_text(entry.type, entry.endian, bytes));
      }
    } else if (!entry.frozen_value.empty()) {
      value_text = QString::fromStdString(
          decode_text(entry.type, entry.endian, entry.frozen_value));
    } else if (!entry.resolved) {
      value_text = "?";
    }
    table_->setItem(row, ColValue, new QTableWidgetItem(value_text));
  }
  table_->blockSignals(false);
  updating_ = false;
  apply_group_filter();
}

void MainWindow::apply_group_filter() {
  const QString needle =
      group_filter_ ? group_filter_->text().trimmed() : QString{};
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (needle.isEmpty()) {
      table_->setRowHidden(row, false);
      continue;
    }
    const auto *group = table_->item(row, ColGroup);
    const auto *description = table_->item(row, ColDescription);
    const QString haystack = (group ? group->text() : QString{}) + ' ' +
                             (description ? description->text() : QString{});
    table_->setRowHidden(row, !haystack.contains(needle, Qt::CaseInsensitive));
  }
}

void MainWindow::on_table_changed(QTableWidgetItem *item) {
  if (updating_ || item == nullptr) {
    return;
  }
  const int row = item->row();
  if (row < 0 || static_cast<std::size_t>(row) >= addresses_.entries().size()) {
    return;
  }
  AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
  if (item->column() == ColHold) {
    if (!entry.resolved) {
      item->setCheckState(Qt::Unchecked);
      QMessageBox::warning(
          this, "GameX", "Спочатку розв'яжіть прив'язку (модуль не знайдено)");
      return;
    }
    if (item->checkState() != Qt::Checked) {
      entry.frozen = false;
      return;
    }
    std::string error;
    if (!try_enable_hold(entry, &error)) {
      item->setCheckState(Qt::Unchecked);
      QMessageBox::warning(this, "GameX", QString::fromStdString(error));
      return;
    }
    return;
  }
  if (item->column() == ColGroup) {
    entry.group = item->text().toStdString();
    apply_group_filter();
    return;
  }
  if (item->column() == ColDescription) {
    entry.description = item->text().toStdString();
    apply_group_filter();
    return;
  }
  if (item->column() == ColClampMin) {
    entry.clamp_min = item->text().trimmed().toStdString();
    if (entry.clamp_min == "-") {
      entry.clamp_min.clear();
    }
    return;
  }
  if (item->column() == ColClampMax) {
    entry.clamp_max = item->text().trimmed().toStdString();
    if (entry.clamp_max == "-") {
      entry.clamp_max.clear();
    }
    return;
  }
  if (item->column() == ColValue && memory_ && is_fixed_width(entry.type)) {
    if (!entry.resolved) {
      QMessageBox::warning(this, "GameX",
                           "Адреса не розв'язана — запис заборонено");
      show_entries();
      return;
    }
    std::string error;
    auto encoded = encode_text(entry.type, entry.endian,
                               item->text().toStdString(), &error);
    if (!encoded) {
      QMessageBox::warning(this, "GameX", QString::fromStdString(error));
      show_entries();
      return;
    }
    const auto io = memory_->write(entry.address, encoded->bytes);
    if (!io.ok() || io.transferred != encoded->bytes.size()) {
      QMessageBox::warning(this, "GameX",
                           "Не вдалося записати значення в пам'ять процесу");
      show_entries();
      return;
    }
    entry.frozen_value = std::move(encoded->bytes);
    write_status(QString("Записано %1 за адресою %2")
                     .arg(item->text())
                     .arg(hex_address(entry.address)));
  }
}

void MainWindow::remove_selected_entries() {
  auto rows = selected_entry_rows();
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
    addresses_.remove_at(static_cast<std::size_t>(*it));
  }
  show_entries();
}

void MainWindow::save_table() {
  QFileDialog dialog(this, "Зберегти таблицю");
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setNameFilter("Таблиця GameX (*.gxt)");
  dialog.setDefaultSuffix("gxt");
  dialog.setFileMode(QFileDialog::AnyFile);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QStringList selected = dialog.selectedFiles();
  if (selected.isEmpty()) {
    return;
  }
  QString path = selected.front();
  if (!path.endsWith(".gxt", Qt::CaseInsensitive)) {
    path += ".gxt";
  }
  std::string error;
  if (!addresses_.save(path.toStdString(), &error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
  }
}

void MainWindow::load_table() {
  const QString path = QFileDialog::getOpenFileName(
      this, "Завантажити таблицю", {}, "Таблиця GameX (*.gxt)");
  if (path.isEmpty()) {
    return;
  }
  std::string error;
  if (!addresses_.load(path.toStdString(), &error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  if (memory_) {
    resolve_addresses();
  } else {
    show_entries();
  }
}

void MainWindow::refresh_views() {
  if (!memory_) {
    return;
  }
  if (!memory_->alive()) {
    statusBar()->showMessage(
        "Процес завершився або PID зайнятий іншим процесом");
    return;
  }
  const FreezeTickStats freeze = freezer_.tick(*memory_, addresses_);
  if (freeze.read_failures > 0 || freeze.write_failures > 0) {
    statusBar()->showMessage(
        QString("Заморозка: активних %1 · помилок читання %2 · запису %3")
            .arg(static_cast<qulonglong>(freeze.active))
            .arg(static_cast<qulonglong>(freeze.read_failures))
            .arg(static_cast<qulonglong>(freeze.write_failures)),
        2000);
  }
  if (table_->is_editing()) {
    return;
  }
  updating_ = true;
  table_->blockSignals(true);
  const int rows = std::min(table_->rowCount(),
                            static_cast<int>(addresses_.entries().size()));
  for (int row = 0; row < rows; ++row) {
    const AddressEntry &entry =
        addresses_.entries()[static_cast<std::size_t>(row)];
    if (!entry.resolved || !is_fixed_width(entry.type)) {
      continue;
    }
    std::vector<std::byte> bytes(fixed_width(entry.type));
    const auto io = memory_->read(entry.address, bytes);
    if (io.transferred != bytes.size()) {
      continue;
    }
    auto *value_item = table_->item(row, ColValue);
    if (value_item == nullptr) {
      continue;
    }
    const QString text =
        QString::fromStdString(decode_text(entry.type, entry.endian, bytes));
    if (value_item->text() != text) {
      value_item->setText(text);
    }
  }
  table_->blockSignals(false);
  updating_ = false;
}

void MainWindow::update_value_fields() {
  const auto op = static_cast<CompareOp>(op_combo_->currentData().toInt());
  const bool needs_value =
      op != CompareOp::Unknown && op != CompareOp::Increased &&
      op != CompareOp::Decreased && op != CompareOp::Changed &&
      op != CompareOp::Unchanged;
  value_edit_->setEnabled(needs_value);
  value_label_->setEnabled(needs_value);
  const bool refining = can_refine_scan();
  if (refining) {
    value_label_->setText(needs_value ? "Нове значення" : "Умова відсіву");
    if (op == CompareOp::Unknown) {
      value_edit_->setPlaceholderText("знімок уже є — оберіть іншу умову");
    } else if (!needs_value) {
      value_edit_->setPlaceholderText(
          "змініть значення в грі, потім «Відсіяти»");
    } else {
      value_edit_->setPlaceholderText("нове значення для відсіву");
    }
  } else {
    value_label_->setText("Що шукати");
    if (op == CompareOp::Unknown) {
      value_edit_->setPlaceholderText("для першого знімка число не потрібне");
    } else if (!needs_value) {
      value_edit_->setPlaceholderText("для цієї умови число не потрібне");
    } else {
      value_edit_->setPlaceholderText("100 · 100*10 · 64h · DE ?? AD");
    }
  }
  const bool between = op == CompareOp::Between;
  upper_edit_->setVisible(between);
  upper_label_->setVisible(between);
}

bool MainWindow::scan_session_active() const { return scan_session_; }

bool MainWindow::can_refine_scan() const {
  if (!scan_session_ || !scanner_ || !memory_ || last_structure_) {
    return false;
  }
  return scanner_->hit_count() > 0 || scanner_->has_snapshot();
}

void MainWindow::update_scan_controls() {
  const bool busy = scanning_;
  const bool have_process = memory_ != nullptr;
  const bool session = scan_session_active();
  const bool refine_ok = can_refine_scan();

  if (scan_button_ != nullptr) {
    // Під час сесії «Шукати» вимкнено — спочатку скинути, щоб не зтерти збіги.
    scan_button_->setEnabled(have_process && !busy && !session);
    scan_button_->setText(session ? "Новий пошук" : "Шукати");
    scan_button_->setToolTip(
        session ? "Спочатку натисніть «Скинути пошук», щоб почати з нуля"
                : "Перший прохід по пам'яті");
  }
  if (refine_button_ != nullptr) {
    refine_button_->setEnabled(have_process && !busy && refine_ok);
    refine_button_->setToolTip(
        !have_process ? "Спочатку оберіть процес"
        : last_structure_
            ? "Після пошуку структури відсів числа недоступний — скиньте пошук"
        : !session   ? "Спочатку виконайте «Шукати»"
        : !refine_ok ? "Немає збігів або знімка для відсіву"
                     : "Залишити лише адреси, що відповідають новій умові");
  }
  if (stop_button_ != nullptr) {
    stop_button_->setEnabled(busy);
  }
  if (reset_button_ != nullptr) {
    reset_button_->setEnabled(have_process && !busy && session);
  }

  if (scan_action_ != nullptr) {
    scan_action_->setEnabled(have_process && !busy && !session);
    scan_action_->setText(session ? "Новий пошук" : "Шукати");
  }
  if (refine_action_ != nullptr) {
    refine_action_->setEnabled(have_process && !busy && refine_ok);
  }
  if (stop_action_ != nullptr) {
    stop_action_->setEnabled(busy);
  }
  if (reset_action_ != nullptr) {
    reset_action_->setEnabled(have_process && !busy && session);
  }

  // Тип/endian фіксуємо на час сесії — інакше показ і відсів роз'їдуться.
  if (type_combo_ != nullptr) {
    type_combo_->setEnabled(!busy && !session);
  }
  if (endian_combo_ != nullptr) {
    endian_combo_->setEnabled(!busy && !session);
  }

  if (hits_hint_ != nullptr) {
    if (refine_ok) {
      hits_hint_->setText(
          "Сесія пошуку активна: змініть умову / значення → «Відсіяти». "
          "Або «Скинути пошук», щоб шукати знову з нуля. Подвійне клацання "
          "або «Додати обране ↓» — у таблицю редагування.");
    } else if (session && last_structure_) {
      hits_hint_->setText(
          "Знайдено бази структур. Додайте поля в таблицю або «Скинути "
          "пошук» для звичайного пошуку числа.");
    } else {
      hits_hint_->setText(
          "Знайдені адреси. Подвійне клацання або кнопка «Додати обране "
          "↓» переносить їх у таблицю редагування.");
    }
  }

  update_value_fields();
}

void MainWindow::write_status(const QString &text) {
  statusBar()->showMessage(text, 4000);
}

std::vector<int> MainWindow::selected_entry_rows() const {
  std::vector<int> rows;
  for (const QTableWidgetSelectionRange &range : table_->selectedRanges()) {
    for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
      if (!table_->isRowHidden(row)) {
        rows.push_back(row);
      }
    }
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}

bool MainWindow::try_enable_hold(AddressEntry &entry, std::string *error) {
  if (!entry.resolved) {
    if (error) {
      *error = "Адреса не розв'язана — утримання заборонено";
    }
    return false;
  }
  if (!memory_ || !memory_->alive()) {
    if (error) {
      *error = "Процес не підключено";
    }
    return false;
  }
  if (!is_fixed_width(entry.type)) {
    if (error) {
      *error = "Утримання працює лише для числових типів";
    }
    return false;
  }
  entry.frozen_value.resize(fixed_width(entry.type));
  const auto io = memory_->read(entry.address, entry.frozen_value);
  if (!io.ok() || io.transferred != entry.frozen_value.size()) {
    entry.frozen_value.clear();
    if (error) {
      *error = "Не вдалося прочитати значення для утримання";
    }
    return false;
  }
  entry.frozen = true;
  return true;
}

void MainWindow::toggle_freeze_selected() {
  const auto rows = selected_entry_rows();
  if (rows.empty()) {
    write_status("F5: виділіть рядок у нижній таблиці");
    return;
  }
  int enabled = 0;
  int disabled = 0;
  for (int row : rows) {
    AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
    if (entry.frozen) {
      entry.frozen = false;
      ++disabled;
      continue;
    }
    std::string error;
    if (!try_enable_hold(entry, &error)) {
      QMessageBox::warning(this, "GameX", QString::fromStdString(error));
      show_entries();
      return;
    }
    ++enabled;
  }
  show_entries();
  write_status(
      QString("F5: увімкнено %1, вимкнено %2").arg(enabled).arg(disabled));
}

void MainWindow::set_value_selected() {
  const auto rows = selected_entry_rows();
  if (rows.empty()) {
    write_status("F6: виділіть рядок у нижній таблиці");
    return;
  }
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Процес не підключено");
    return;
  }
  bool ok = false;
  const QString text = QInputDialog::getText(
      this, "Задати значення",
      "Нове значення для виділених адрес:", QLineEdit::Normal, {}, &ok);
  if (!ok || text.trimmed().isEmpty()) {
    return;
  }
  int written = 0;
  for (int row : rows) {
    AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
    if (!entry.resolved) {
      QMessageBox::warning(this, "GameX",
                           "Адреса не розв'язана — запис заборонено");
      show_entries();
      return;
    }
    if (!is_fixed_width(entry.type)) {
      continue;
    }
    std::string error;
    auto encoded =
        encode_text(entry.type, entry.endian, text.toStdString(), &error);
    if (!encoded) {
      QMessageBox::warning(this, "GameX", QString::fromStdString(error));
      show_entries();
      return;
    }
    const auto io = memory_->write(entry.address, encoded->bytes);
    if (!io.ok() || io.transferred != encoded->bytes.size()) {
      QMessageBox::warning(this, "GameX", "Не вдалося записати значення");
      show_entries();
      return;
    }
    entry.frozen_value = std::move(encoded->bytes);
    ++written;
  }
  show_entries();
  write_status(QString("F6: записано в %1 адрес(и)").arg(written));
}

void MainWindow::cycle_freeze_mode_selected() {
  const auto rows = selected_entry_rows();
  if (rows.empty()) {
    write_status("F7: виділіть рядок у нижній таблиці");
    return;
  }
  constexpr FreezeMode kModes[] = {
      FreezeMode::Normal, FreezeMode::AllowIncrease, FreezeMode::AllowDecrease,
      FreezeMode::Clamp};
  for (int row : rows) {
    AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
    int index = 0;
    for (int i = 0; i < 4; ++i) {
      if (kModes[i] == entry.freeze_mode) {
        index = i;
        break;
      }
    }
    entry.freeze_mode = kModes[(index + 1) % 4];
  }
  show_entries();
  const auto label = freeze_mode_label(
      addresses_.entries()[static_cast<std::size_t>(rows.front())].freeze_mode);
  write_status(QString("F7: режим «%1»")
                   .arg(QString::fromUtf8(label.data(),
                                          static_cast<int>(label.size()))));
}

std::size_t MainWindow::resolve_addresses() {
  if (!memory_) {
    show_entries();
    return addresses_.entries().size();
  }
  const ModuleMap modules = ModuleMap::from_process(*memory_);
  const std::size_t failed = addresses_.resolve_all(*memory_, modules);
  show_entries();
  return failed;
}

void MainWindow::bind_selected_to_module() {
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  const auto rows = selected_entry_rows();
  if (rows.empty()) {
    write_status("Виділіть рядок у нижній таблиці");
    return;
  }
  const ModuleMap modules = ModuleMap::from_process(*memory_);
  int bound = 0;
  for (int row : rows) {
    AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
    if (bind_to_module(entry, modules)) {
      ++bound;
    }
  }
  show_entries();
  write_status(QString("Прив'язано до модуля: %1").arg(bound));
}

void MainWindow::open_hex_for_selected() {
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  std::uintptr_t address = 0;
  const auto entry_rows = selected_entry_rows();
  if (entry_rows.size() == 1) {
    address = addresses_.entries()[static_cast<std::size_t>(entry_rows.front())]
                  .address;
  } else if (results_->currentRow() >= 0 &&
             results_->item(results_->currentRow(), 0) != nullptr) {
    address =
        static_cast<std::uintptr_t>(results_->item(results_->currentRow(), 0)
                                        ->data(Qt::UserRole)
                                        .toULongLong());
  } else {
    QMessageBox::information(
        this, "GameX", "Виділіть адресу в таблиці збігів або збережених адрес");
    return;
  }
  HexDialog dialog(memory_.get(), address, this);
  dialog.exec();
  show_entries();
}

void MainWindow::open_help() {
  HelpDialog dialog(this);
  dialog.exec();
}

void MainWindow::open_about() {
  const QString text =
      QStringLiteral(
          "<h3>GameX %1</h3>"
          "<p>Редактор пам'яті локальних однокористувацьких ігор на Linux.</p>"
          "<p>"
          "Версія програми: <b>%1</b> <br/>"
          "Час останньої збірки: <b>%2</b>"
          "</p>"
          "<p>© 2026 Ihor Dolinskyi<br>"
          "Ліцензія: GNU GPL версії 3 або пізнішої.<br>"
          "Подробиці про сторонні компоненти — у файлі NOTICE.</p>")
          .arg(QStringLiteral(GAMEX_VERSION_STRING),
               QStringLiteral(GAMEX_BUILD_TIME_READABLE));
  QMessageBox::about(this, "Про програму", text);
}

void MainWindow::open_about_qt() { QMessageBox::aboutQt(this, "Про Qt"); }

void MainWindow::disconnect_process() {
  if (!memory_) {
    write_status("Процес уже не підключено");
    return;
  }
  if (scanning_) {
    QMessageBox::information(this, "GameX", "Спочатку зупиніть пошук");
    return;
  }
  detach_process("Процес від'єднано");
}

const EmulatorProfile *MainWindow::current_emulator() const {
  if (emulator_combo_ == nullptr) {
    return nullptr;
  }
  const QString id = emulator_combo_->currentData().toString();
  if (id.isEmpty()) {
    return nullptr;
  }
  return find_emulator_profile(id.toStdString());
}

void MainWindow::on_emulator_changed() {
  const EmulatorProfile *profile = current_emulator();
  if (profile == nullptr) {
    emulator_ram_label_->setText("RAM: —");
    return;
  }
  for (int i = 0; i < endian_combo_->count(); ++i) {
    if (endian_combo_->itemData(i).toInt() ==
        static_cast<int>(profile->endian)) {
      endian_combo_->setCurrentIndex(i);
      break;
    }
  }
  if (memory_) {
    detect_emulator_ram();
  } else {
    emulator_ram_label_->setText(QString("RAM: %1 (немає процесу)")
                                     .arg(QString::fromStdString(profile->id)));
  }
}

void MainWindow::detect_emulator_ram() {
  const EmulatorProfile *profile = current_emulator();
  if (profile == nullptr) {
    emulator_ram_label_->setText("RAM: —");
    write_status("Оберіть профіль емулятора");
    return;
  }
  if (!memory_) {
    QMessageBox::information(this, "GameX",
                             "Спочатку підключіть процес емулятора");
    return;
  }
  auto candidates = find_guest_ram_candidates(*memory_, *profile,
                                              limit_region_ ? region_start_ : 0,
                                              limit_region_ ? region_end_ : 0);
  if (candidates.empty()) {
    emulator_ram_label_->setText("RAM: не знайдено");
    QMessageBox::warning(
        this, "GameX",
        QString("Не знайдено регіон для «%1». Завантажте гру або обмежте "
                "пошук кнопкою «Блок пам'яті».")
            .arg(QString::fromStdString(profile->title)));
    return;
  }

  int chosen = 0;
  if (candidates.size() > 1) {
    QStringList items;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const GuestRamMapping &c = candidates[i];
      const QString path = c.pathname.empty()
                               ? QString("[анонімний]")
                               : QString::fromStdString(c.pathname);
      items << QString("%1) 0x%2 · %3 КіБ · %4")
                   .arg(static_cast<qulonglong>(i + 1))
                   .arg(static_cast<qulonglong>(c.host_base), 0, 16)
                   .arg(static_cast<qulonglong>(c.size() / 1024))
                   .arg(path);
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(
        this, "Кілька кандидатів RAM",
        QString("Знайдено %1 регіонів під «%2». Оберіть правильний блок "
                "(pathname з /proc/…/maps):")
            .arg(candidates.size())
            .arg(QString::fromStdString(profile->title)),
        items, 0, false, &ok);
    if (!ok) {
      return;
    }
    const int index = static_cast<int>(items.indexOf(picked));
    chosen = index < 0 ? 0 : index;
  }

  const GuestRamMapping &mapping = candidates[static_cast<std::size_t>(chosen)];
  limit_region_ = true;
  region_start_ = mapping.host_base;
  region_end_ = mapping.host_end;
  const QString path = mapping.pathname.empty()
                           ? QString("[анонімний]")
                           : QString::fromStdString(mapping.pathname);
  region_label_->setText(
      QString("Область: 0x%1 (%2)")
          .arg(static_cast<qulonglong>(mapping.host_base), 0, 16)
          .arg(path));
  emulator_ram_label_->setText(
      QString("RAM: 0x%1 (%2 КіБ) %3")
          .arg(static_cast<qulonglong>(mapping.host_base), 0, 16)
          .arg(static_cast<qulonglong>(mapping.size() / 1024))
          .arg(path));
  QString status = QString("Гостьова база 0x%1 → host 0x%2 · %3")
                       .arg(static_cast<qulonglong>(profile->guest_base), 0, 16)
                       .arg(static_cast<qulonglong>(mapping.host_base), 0, 16)
                       .arg(path);
  if (candidates.size() > 1) {
    status +=
        QString(" (обрано %1 з %2)").arg(chosen + 1).arg(candidates.size());
  }
  if (!profile->path_contains.empty()) {
    status += QString(" · path hint «%1»")
                  .arg(QString::fromStdString(profile->path_contains));
  }
  write_status(status);
}

bool MainWindow::bind_entry_to_emulator(AddressEntry &entry,
                                        std::uintptr_t host_address,
                                        std::string *error) {
  const EmulatorProfile *profile = current_emulator();
  if (profile == nullptr || !memory_) {
    if (error) {
      *error = "Немає профілю емулятора";
    }
    return false;
  }
  const auto mapping = locate_guest_ram(*memory_, *profile, error,
                                        limit_region_ ? region_start_ : 0,
                                        limit_region_ ? region_end_ : 0);
  if (!mapping) {
    return false;
  }
  const auto guest = host_to_guest(*mapping, *profile, host_address);
  if (!guest) {
    if (error) {
      *error = "Адреса поза блоком гостьової RAM обраного профілю";
    }
    return false;
  }
  entry.emulator = profile->id;
  entry.guest_address = *guest;
  entry.address = host_address;
  entry.module.clear();
  entry.static_offset = 0;
  entry.pointer_offsets.clear();
  entry.endian = static_cast<Endian>(endian_combo_->currentData().toInt());
  entry.resolved = true;
  return true;
}

void MainWindow::open_watch_for_selected() {
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  if (launched_pid_ < 0 || memory_->pid() != launched_pid_) {
    QMessageBox::information(this, "GameX",
                             "Watchpoint доступний лише для процесу, "
                             "запущеного кнопкою «Запустити».");
    return;
  }
  if (scanning_) {
    QMessageBox::information(this, "GameX", "Спочатку зупиніть пошук");
    return;
  }
  std::uintptr_t address = 0;
  std::size_t size = 4;
  const auto entry_rows = selected_entry_rows();
  if (entry_rows.size() == 1) {
    const AddressEntry &entry =
        addresses_.entries()[static_cast<std::size_t>(entry_rows.front())];
    if (!entry.resolved) {
      QMessageBox::warning(this, "GameX", "Адреса ще не розв'язана");
      return;
    }
    address = entry.address;
    if (is_fixed_width(entry.type)) {
      const std::size_t width = fixed_width(entry.type);
      if (width == 1 || width == 2 || width == 4 || width == 8) {
        size = width;
      } else {
        size = 1;
      }
    }
  } else if (results_->currentRow() >= 0 &&
             results_->item(results_->currentRow(), 0) != nullptr) {
    address =
        static_cast<std::uintptr_t>(results_->item(results_->currentRow(), 0)
                                        ->data(Qt::UserRole)
                                        .toULongLong());
    const auto type =
        static_cast<ValueType>(type_combo_->currentData().toInt());
    if (is_fixed_width(type)) {
      const std::size_t width = fixed_width(type);
      size = (width == 1 || width == 2 || width == 4 || width == 8) ? width : 1;
    }
  } else {
    QMessageBox::information(
        this, "GameX", "Виділіть адресу в таблиці збігів або збережених адрес");
    return;
  }
  if (address % size != 0) {
    size = 1;
  }
  const bool was_active = timer_->isActive();
  timer_->stop();
  WatchDialog dialog(memory_->pid(), address, size, this);
  dialog.exec();
  if (was_active) {
    timer_->start();
  }
  show_entries();
}

void MainWindow::open_structure_dialog() {
  if (!scanner_ || !memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  if (scanning_) {
    QMessageBox::information(this, "GameX", "Спочатку зупиніть пошук");
    return;
  }
  std::vector<AddressEntry> draft;
  for (int row : selected_entry_rows()) {
    draft.push_back(addresses_.entries()[static_cast<std::size_t>(row)]);
  }
  StructureDialog dialog(memory_.get(), draft, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  ScanQuery region;
  region.alignment = dialog.alignment();
  region.writable_only = writable_only_->isChecked();
  const int mib = max_region_mib_->value();
  region.max_region_bytes =
      mib <= 0 ? 0 : static_cast<std::uint64_t>(mib) * 1024ull * 1024ull;
  if (limit_region_) {
    region.region_start = region_start_;
    region.region_end = region_end_;
  }
  region.skip_path = executable_path(static_cast<pid_t>(::getpid()));

  const StructureTemplate tmpl = dialog.structure();
  last_structure_ = tmpl;
  last_scan_type_ =
      tmpl.fields.empty() ? ValueType::Int32 : tmpl.fields.front().type;
  last_scan_endian_ = tmpl.endian;
  start_scan_worker(std::move(region), false, tmpl);
}

void MainWindow::capture_dump_slot(int slot) {
  if (!scanner_ || !memory_) {
    QMessageBox::information(this, "GameX", "Немає збігів для знімка");
    return;
  }
  if (last_structure_) {
    QMessageBox::information(
        this, "GameX",
        "Знімки M1–M4 для формул працюють після звичайного пошуку числа, "
        "не після пошуку структури.");
    return;
  }
  std::string error;
  if (!dumps_.capture_hits(slot, *scanner_, *memory_, last_scan_type_,
                           last_scan_endian_, &error)) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(error));
    return;
  }
  update_dump_indicators();
  write_status(QString("Знімок M%1: %2 адрес")
                   .arg(slot + 1)
                   .arg(static_cast<qulonglong>(dumps_.size(slot))));
}

void MainWindow::update_dump_indicators() {
  for (int i = 0; i < kDumpSlots; ++i) {
    QPushButton *button = dump_buttons_[static_cast<std::size_t>(i)];
    if (button == nullptr) {
      continue;
    }
    const bool ready = dumps_.has_slot(i);
    const std::size_t count = dumps_.size(i);
    button->setText(ready ? QString("M%1✓").arg(i + 1)
                          : QString("M%1").arg(i + 1));
    button->setToolTip(
        ready ? QString("Знімок M%1: %2 адрес (клік — перезаписати)")
                    .arg(i + 1)
                    .arg(static_cast<qulonglong>(count))
              : QString("Зберегти поточні значення збігів як знімок M%1")
                    .arg(i + 1));
  }
}

void MainWindow::refine_by_formula() {
  if (!scanner_ || !memory_) {
    QMessageBox::information(
        this, "GameX",
        "Спочатку виконайте пошук (Шукати), щоб був список збігів.\n"
        "Потім збережіть знімки M1…M4 і відсійте формулою.");
    return;
  }
  const QString formula = formula_edit_->text().trimmed();
  if (formula.isEmpty()) {
    QMessageBox::information(this, "GameX",
                             "Введіть або оберіть формулу.\n\n"
                             "Приклад:\n"
                             "  1) M1 — зберегти значення\n"
                             "  2) змінити число в грі\n"
                             "  3) M2 — другий знімок\n"
                             "  4) формула M1<>M2 → «Відсіяти формулою»\n\n"
                             "Кнопка «?» відкриває докладну довідку.");
    return;
  }
  std::string error;
  const ScanStatus status =
      dumps_.refine_formula(*scanner_, *memory_, last_scan_type_,
                            last_scan_endian_, formula.toStdString(), &error);
  if (!status.ok) {
    QMessageBox::warning(this, "GameX", QString::fromStdString(status.message));
    return;
  }
  show_hits();
  write_status(QString("Формула «%1»: залишилось %2")
                   .arg(formula)
                   .arg(static_cast<qulonglong>(status.hit_count)));
}

void MainWindow::open_dump_help() {
  HelpDialog dialog(QStringLiteral("Формули M1–M4"), this);
  dialog.exec();
}

void MainWindow::find_pointers_for_selected() {
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  const auto rows = selected_entry_rows();
  if (rows.size() != 1) {
    QMessageBox::information(this, "GameX",
                             "Виділіть рівно один рядок з адресою значення");
    return;
  }
  AddressEntry &source =
      addresses_.entries()[static_cast<std::size_t>(rows.front())];
  if (!source.resolved) {
    QMessageBox::warning(this, "GameX", "Адреса ще не розв'язана");
    return;
  }
  PointerDialog dialog(memory_.get(), source.address, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  for (const PointerPath &path : dialog.selected_paths()) {
    AddressEntry entry = source;
    entry.module = path.module;
    entry.static_offset = path.static_offset;
    entry.pointer_offsets = path.offsets;
    entry.address = path.resolved;
    entry.resolved = true;
    entry.description = source.description + " ptr";
    entry.frozen = false;
    addresses_.add(std::move(entry));
  }
  show_entries();
  write_status(
      QString("Додано шляхів: %1").arg(dialog.selected_paths().size()));
}

void MainWindow::capture_value_selected() {
  const auto rows = selected_entry_rows();
  if (rows.empty() || !memory_) {
    write_status("F8: виділіть рядок і підключіть процес");
    return;
  }
  int captured = 0;
  for (int row : rows) {
    AddressEntry &entry = addresses_.entries()[static_cast<std::size_t>(row)];
    if (!is_fixed_width(entry.type)) {
      continue;
    }
    entry.frozen_value.resize(fixed_width(entry.type));
    const auto io = memory_->read(entry.address, entry.frozen_value);
    if (io.transferred == entry.frozen_value.size()) {
      ++captured;
    }
  }
  show_entries();
  write_status(
      QString("F8: запам'ятовано поточне значення (%1)").arg(captured));
}

void MainWindow::choose_region() {
  if (!memory_) {
    QMessageBox::information(this, "GameX", "Спочатку оберіть процес");
    return;
  }
  RegionDialog dialog(memory_->regions(), this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  if (dialog.whole_process() || dialog.selected() == nullptr) {
    limit_region_ = false;
    region_label_->setText("Область: увесь процес");
    return;
  }
  const MemoryRegion &region = *dialog.selected();
  limit_region_ = true;
  region_start_ = region.start;
  region_end_ = region.end;
  const QString path = region.pathname.empty()
                           ? QString("анонімний")
                           : QString::fromStdString(region.pathname);
  region_label_->setText(QString("Область: 0x%1 (%2)")
                             .arg(static_cast<qulonglong>(region.start), 0, 16)
                             .arg(path));
}

void MainWindow::stop_scan() { cancel_.store(true); }

void MainWindow::set_scanning(bool scanning) {
  scanning_ = scanning;
  update_scan_controls();
}

void MainWindow::detach_process(const QString &reason) {
  wait_for_scan_thread();
  scanning_ = false;
  scan_session_ = false;
  scanner_.reset();
  memory_.reset();
  limit_region_ = false;
  dumps_.clear();
  update_dump_indicators();
  if (emulator_ram_label_ != nullptr) {
    emulator_ram_label_->setText("RAM: —");
  }
  process_label_->setText("Процес не обрано");
  region_label_->setText("Область: увесь процес");
  results_->setRowCount(0);
  statusBar()->showMessage(reason);
  update_scan_controls();
}

void MainWindow::on_scan_progress(qulonglong scanned, qulonglong total) {
  if (total == 0) {
    progress_->setRange(0, 0);
    return;
  }
  progress_->setRange(0, 1000);
  const int value =
      static_cast<int>(std::min<qulonglong>(1000, scanned * 1000 / total));
  progress_->setValue(value);
  progress_->setFormat(
      QString("%1 / %2 МБ")
          .arg(static_cast<double>(scanned) / (1024.0 * 1024.0), 0, 'f', 1)
          .arg(static_cast<double>(total) / (1024.0 * 1024.0), 0, 'f', 1));
}

void MainWindow::on_scan_completed() {
  ScanStatus status;
  {
    std::lock_guard lock(status_mutex_);
    status = last_status_;
  }
  scanning_ = false;
  progress_->setRange(0, 1000);
  progress_->setValue(status.cancelled ? progress_->value() : 1000);
  if (!status.ok) {
    // Невдалий перший прохід / структура — не лишаємо «завислі» шаблони.
    if (!scan_session_) {
      last_structure_.reset();
    }
    progress_->setFormat("Помилка");
    statusBar()->showMessage(QString::fromStdString(status.message));
    QMessageBox::warning(this, "GameX", QString::fromStdString(status.message));
    update_scan_controls();
    return;
  }
  const bool useful = status.hit_count > 0 || status.snapshot ||
                      (last_structure_ && status.hit_count > 0);
  // Знімок unknown без хітів теж відкриває сесію (далі лише «Відсіяти»).
  if (useful || status.snapshot) {
    scan_session_ = true;
  } else if (last_structure_) {
    last_structure_.reset();
  }
  show_hits();
  QString message;
  if (status.snapshot && status.hit_count == 0) {
    message =
        QString("Знімок %1 МБ. Змініть значення в грі і натисніть «Відсіяти».")
            .arg(static_cast<double>(status.scanned) / (1024.0 * 1024.0), 0,
                 'f', 1);
  } else if (last_structure_) {
    message = QString("Структура «%1»: %2 баз")
                  .arg(QString::fromStdString(last_structure_->name))
                  .arg(static_cast<qulonglong>(status.hit_count));
  } else {
    message =
        QString("Знайдено %1").arg(static_cast<qulonglong>(status.hit_count));
    if (scanner_ && scanner_->hit_count() > 5000) {
      message += QString(" (показано перші %1)").arg(5000);
    }
    if (status.hit_count > 0) {
      message += " · далі «Відсіяти» або «Скинути пошук»";
    }
  }
  if (status.cancelled) {
    message += " (зупинено)";
  }
  if (status.truncated) {
    message += " (досягнуто ліміт)";
  }
  if (!status.message.empty() && status.hit_count == 0 && !status.snapshot) {
    message = QString::fromStdString(status.message);
  }
  progress_->setFormat(message);
  statusBar()->showMessage(message);
  update_scan_controls();
}

} // namespace gamex
