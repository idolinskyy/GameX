#include "ui/process_dialog.hpp"

#include "core/process_memory.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <unistd.h>

namespace gamex {

ProcessDialog::ProcessDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Процеси");
  resize(960, 540);

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText("Пошук за pid, користувачем, ім'ям або командою");
  filter_->setClearButtonEnabled(true);

  login_users_only_ =
      new QCheckBox(QString("Лише користувачі входу (UID ≥ %1)")
                        .arg(static_cast<qulonglong>(login_uid_min())),
                    this);
  login_users_only_->setChecked(true);
  login_users_only_->setToolTip(
      "Без системних служб (daemon/root з низьким UID). Поріг з "
      "/etc/login.defs (UID_MIN).");

  my_uid_only_ = new QCheckBox("Лише мій користувач (поточний UID)", this);
  my_uid_only_->setChecked(::geteuid() != 0);
  my_uid_only_->setToolTip(
      "Залишити лише процеси вашого uid. Найбільше зменшує список. "
      "Під root за замовчуванням вимкнено — інакше майже нічого не видно.");

  panel_only_ =
      new QCheckBox("Лише з панелі оболонки (запущені програми)", this);
  panel_only_->setChecked(true);
  panel_only_->setToolTip(
      "Лише програми з іконками на панелі задач (вікна оболонки через X11). "
      "Потрібен DISPLAY (X11 або XWayland). На чистому Wayland без XWayland "
      "список може вийти порожнім.");

  auto *note = new QLabel(
      "У /proc на сучасних ядрах уже лише головні процеси — окремі потоки "
      "там не дублюються. Колонка «Потоки» — скільки їх усередині. "
      "Фільтр панелі показує те, що видно на панелі задач, а не весь /proc.",
      this);
  note->setWordWrap(true);
  note->setStyleSheet("color: #888;");

  empty_hint_ = new QLabel(this);
  empty_hint_->setWordWrap(true);
  empty_hint_->setStyleSheet("color: #a06020;");
  empty_hint_->setVisible(false);

  table_ = new QTableWidget(this);
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels(
      {"PID", "Користувач", "Потоки", "Ім'я", "Командний рядок"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::ResizeToContents);

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
  connect(login_users_only_, &QCheckBox::toggled, this,
          [this](bool) { apply_filter(); });
  connect(my_uid_only_, &QCheckBox::toggled, this,
          [this](bool) { apply_filter(); });
  connect(panel_only_, &QCheckBox::toggled, this,
          [this](bool) { apply_filter(); });

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(filter_);
  layout->addWidget(login_users_only_);
  layout->addWidget(my_uid_only_);
  layout->addWidget(panel_only_);
  layout->addWidget(note);
  layout->addWidget(empty_hint_);
  layout->addWidget(table_);
  layout->addWidget(buttons);
  load_settings();
  reload();
  filter_->setFocus();
}

ProcessDialog::~ProcessDialog() { save_settings(); }

void ProcessDialog::load_settings() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("processDialog"));
  if (settings.contains(QStringLiteral("geometry"))) {
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
  }
  if (filter_ != nullptr && settings.contains(QStringLiteral("filter"))) {
    filter_->setText(settings.value(QStringLiteral("filter")).toString());
  }
  if (login_users_only_ != nullptr &&
      settings.contains(QStringLiteral("loginUsersOnly"))) {
    login_users_only_->setChecked(
        settings.value(QStringLiteral("loginUsersOnly")).toBool());
  }
  if (my_uid_only_ != nullptr && settings.contains(QStringLiteral("myUidOnly"))) {
    my_uid_only_->setChecked(
        settings.value(QStringLiteral("myUidOnly")).toBool());
  }
  if (panel_only_ != nullptr &&
      settings.contains(QStringLiteral("panelOnly"))) {
    panel_only_->setChecked(
        settings.value(QStringLiteral("panelOnly")).toBool());
  }
  settings.endGroup();
}

void ProcessDialog::save_settings() const {
  QSettings settings;
  settings.beginGroup(QStringLiteral("processDialog"));
  settings.setValue(QStringLiteral("geometry"), saveGeometry());
  if (filter_ != nullptr) {
    settings.setValue(QStringLiteral("filter"), filter_->text());
  }
  if (login_users_only_ != nullptr) {
    settings.setValue(QStringLiteral("loginUsersOnly"),
                      login_users_only_->isChecked());
  }
  if (my_uid_only_ != nullptr) {
    settings.setValue(QStringLiteral("myUidOnly"), my_uid_only_->isChecked());
  }
  if (panel_only_ != nullptr) {
    settings.setValue(QStringLiteral("panelOnly"), panel_only_->isChecked());
  }
  settings.endGroup();
}

void ProcessDialog::reload() {
  processes_ = list_processes();
  panel_pids_.clear();
  for (pid_t pid : list_shell_panel_pids()) {
    if (pid > 0) {
      panel_pids_.insert(pid);
    }
  }
  apply_filter();
}

void ProcessDialog::apply_filter() {
  const QString needle = filter_->text().trimmed();
  const bool login_only =
      login_users_only_ != nullptr && login_users_only_->isChecked();
  const bool mine_only = my_uid_only_ != nullptr && my_uid_only_->isChecked();
  const bool panel_only = panel_only_ != nullptr && panel_only_->isChecked();
  const uid_t self = ::geteuid();
  table_->setRowCount(0);
  int row = 0;
  for (const ProcessInfo &info : processes_) {
    if (login_only && !is_login_user_uid(info.uid)) {
      continue;
    }
    if (mine_only && info.uid != self) {
      continue;
    }
    if (panel_only && !panel_pids_.contains(info.pid)) {
      continue;
    }
    const QString pid = QString::number(info.pid);
    const QString user = QString::fromStdString(info.user);
    const QString threads = QString::number(info.thread_count);
    const QString name = QString::fromStdString(info.name);
    const QString command = QString::fromStdString(info.command_line);
    const bool match = needle.isEmpty() || pid.contains(needle) ||
                       user.contains(needle, Qt::CaseInsensitive) ||
                       name.contains(needle, Qt::CaseInsensitive) ||
                       command.contains(needle, Qt::CaseInsensitive);
    if (!match) {
      continue;
    }
    table_->insertRow(row);
    auto *pid_item = new QTableWidgetItem(pid);
    pid_item->setData(Qt::UserRole, static_cast<qlonglong>(info.pid));
    table_->setItem(row, 0, pid_item);
    table_->setItem(row, 1, new QTableWidgetItem(user));
    auto *thr = new QTableWidgetItem(threads);
    thr->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 2, thr);
    table_->setItem(row, 3, new QTableWidgetItem(name));
    table_->setItem(row, 4, new QTableWidgetItem(command));
    ++row;
  }
  if (table_->rowCount() > 0) {
    table_->selectRow(0);
    if (empty_hint_ != nullptr) {
      empty_hint_->clear();
      empty_hint_->setVisible(false);
    }
  } else if (empty_hint_ != nullptr) {
    if (panel_only && panel_pids_.empty()) {
      empty_hint_->setText(
          "Фільтр панелі нічого не знайшов. Потрібен X11 або XWayland "
          "(змінна DISPLAY). На чистому Wayland вимкніть "
          "«Лише з панелі оболонки» або шукайте за назвою вище.");
      empty_hint_->setVisible(true);
    } else if (panel_only) {
      empty_hint_->setText(
          "Немає збігів із поточними фільтрами. Спробуйте вимкнути "
          "«Лише з панелі» або «Лише мій користувач», або очистіть пошук.");
      empty_hint_->setVisible(true);
    } else {
      empty_hint_->setText(
          "Немає процесів за поточними фільтрами. Послабте умови або "
          "натисніть «Оновити».");
      empty_hint_->setVisible(true);
    }
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
