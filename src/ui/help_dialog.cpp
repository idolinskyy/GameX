#include "ui/help_dialog.hpp"

#include "ui/help_content.hpp"

#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace gamex {

QString HelpDialog::page_chrome(const QString &body_html) {
  return QStringLiteral(
             "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
             "body{font-family:'Noto Sans','DejaVu Sans',Ubuntu,sans-serif;"
             "font-size:16px;line-height:1.55;color:#1a1d23;background:#f4f5f7;"
             "margin:0;padding:1.1em 1.4em 2em;}"
             "h1{font-size:1.45em;color:#12151a;margin:0 0 "
             "0.6em;font-weight:700;}"
             "h2{font-size:1.12em;color:#1a2330;margin:1.2em 0 "
             "0.45em;font-weight:650;}"
             "p,li{color:#1a1d23;}"
             "ul,ol{padding-left:1.35em;}"
             "li{margin:0.38em 0;}"
             "code{font-family:'Noto Sans Mono','DejaVu Sans Mono',monospace;"
             "font-size:0.92em;background:#e4e7ec;color:#12151a;"
             "padding:0.12em 0.35em;border-radius:3px;}"
             ".tip,.warn,.ok{padding:0.75em 0.95em;margin:0.95em 0;"
             "border-radius:6px;color:#1a1d23;}"
             ".tip{background:#efe6c8;border:1px solid #d4bc6a;}"
             ".warn{background:#f0d6d6;border:1px solid #d09090;}"
             ".ok{background:#d8e8d8;border:1px solid #8bbb8b;}"
             ".tip b,.warn b,.ok b{color:#12151a;}"
             ".bad{color:#a02020;font-weight:600;}"
             "a{color:#1a5f9e;text-decoration:none;}"
             "a:hover{text-decoration:underline;}"
             "</style></head><body>%1</body></html>")
      .arg(body_html);
}

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent) { finish_setup(0); }

HelpDialog::HelpDialog(const QString &section_title, QWidget *parent)
    : QDialog(parent) {
  finish_setup(section_index_by_title(section_title));
}

int HelpDialog::section_index_by_title(const QString &title) {
  const HelpSection *sections = help_sections();
  const int count = static_cast<int>(help_section_count());
  for (int i = 0; i < count; ++i) {
    if (title == QString::fromUtf8(sections[i].title)) {
      return i;
    }
  }
  return 0;
}

void HelpDialog::finish_setup(int initial_row) {
  setWindowTitle("Довідка GameX");
  resize(1020, 740);
  setMinimumSize(760, 520);

  QPalette dlg = palette();
  dlg.setColor(QPalette::Window, QColor(0x2a2f38));
  dlg.setColor(QPalette::WindowText, QColor(0xe8eaed));
  dlg.setColor(QPalette::Base, QColor(0xf4f5f7));
  dlg.setColor(QPalette::Text, QColor(0x1a1d23));
  dlg.setColor(QPalette::Button, QColor(0x3a424e));
  dlg.setColor(QPalette::ButtonText, QColor(0xe8eaed));
  dlg.setColor(QPalette::Highlight, QColor(0x3d7ea6));
  dlg.setColor(QPalette::HighlightedText, QColor(0xffffff));
  setPalette(dlg);

  auto *title = new QLabel("Довідка", this);
  {
    QFont f = title->font();
    f.setPointSize(qMax(12, f.pointSize() + 2));
    f.setBold(true);
    title->setFont(f);
  }
  title->setStyleSheet("color:#e8eaed; padding: 4px 2px 4px 2px;");

  auto *subtitle = new QLabel(
      "Оберіть кейс зліва. Посилання в тексті відкривають інший розділ.",
      this);
  subtitle->setWordWrap(true);
  subtitle->setStyleSheet("color:#9aa3ad; padding: 0 2px 8px 2px; font-size:12px;");

  nav_filter_ = new QLineEdit(this);
  nav_filter_->setPlaceholderText("Фільтр розділів…");
  nav_filter_->setClearButtonEnabled(true);
  nav_filter_->setStyleSheet(
      "QLineEdit { background:#1c2128; color:#e8eaed; border:1px solid #3a424e;"
      "  border-radius:4px; padding:6px 8px; }");

  nav_ = new QListWidget(this);
  nav_->setMinimumWidth(240);
  nav_->setStyleSheet("QListWidget {"
                      "  background:#232830; color:#d4d8de; border:none;"
                      "  outline:none; font-size:14px; padding:6px 0;"
                      "}"
                      "QListWidget::item {"
                      "  padding:9px 14px; margin:1px 6px; border-radius:6px;"
                      "}"
                      "QListWidget::item:selected {"
                      "  background:#3d7ea6; color:#ffffff;"
                      "}"
                      "QListWidget::item:hover:!selected {"
                      "  background:#2f3640;"
                      "}");
  const HelpSection *sections = help_sections();
  const int count = static_cast<int>(help_section_count());
  for (int i = 0; i < count; ++i) {
    auto *item = new QListWidgetItem(QString::fromUtf8(sections[i].title));
    item->setData(Qt::UserRole, i);
    nav_->addItem(item);
  }

  browser_ = new QTextBrowser(this);
  browser_->setOpenExternalLinks(false);
  browser_->setOpenLinks(false);
  browser_->setFrameShape(QFrame::NoFrame);
  {
    QPalette pal = browser_->palette();
    pal.setColor(QPalette::Base, QColor(0xf4f5f7));
    pal.setColor(QPalette::Text, QColor(0x1a1d23));
    pal.setColor(QPalette::WindowText, QColor(0x1a1d23));
    pal.setColor(QPalette::Link, QColor(0x1a5f9e));
    browser_->setPalette(pal);
    QFont font = browser_->font();
    font.setPointSize(qMax(11, font.pointSize() + 1));
    browser_->setFont(font);
  }

  page_find_ = new QLineEdit(this);
  page_find_->setPlaceholderText("Знайти на сторінці… (Enter)");
  page_find_->setClearButtonEnabled(true);
  auto *find_button = new QPushButton("Знайти", this);
  find_button->setToolTip("Шукати далі на цій сторінці (як Ctrl+F)");

  auto *find_row = new QHBoxLayout();
  find_row->addWidget(page_find_, 1);
  find_row->addWidget(find_button);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->button(QDialogButtonBox::Close)->setText("Закрити");
  buttons->setStyleSheet("QPushButton { background:#3a424e; color:#e8eaed; "
                         "border:1px solid #505868;"
                         "  padding:6px 16px; border-radius:4px; }"
                         "QPushButton:hover { background:#4a5462; }");

  connect(nav_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *current, QListWidgetItem *) {
            if (current == nullptr) {
              return;
            }
            show_section(current->data(Qt::UserRole).toInt());
          });
  connect(nav_filter_, &QLineEdit::textChanged, this,
          &HelpDialog::filter_nav);
  connect(find_button, &QPushButton::clicked, this, &HelpDialog::find_in_page);
  connect(page_find_, &QLineEdit::returnPressed, this,
          &HelpDialog::find_in_page);
  connect(browser_, &QTextBrowser::anchorClicked, this,
          &HelpDialog::on_anchor);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  auto *find_shortcut = new QShortcut(QKeySequence::Find, this);
  connect(find_shortcut, &QShortcut::activated, this, [this] {
    page_find_->setFocus();
    page_find_->selectAll();
  });

  auto *left = new QWidget(this);
  auto *left_layout = new QVBoxLayout(left);
  left_layout->setContentsMargins(10, 10, 4, 10);
  left_layout->setSpacing(4);
  left_layout->addWidget(title);
  left_layout->addWidget(subtitle);
  left_layout->addWidget(nav_filter_);
  left_layout->addWidget(nav_, 1);

  auto *right = new QWidget(this);
  auto *right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(4, 10, 10, 10);
  right_layout->setSpacing(8);
  right_layout->addWidget(browser_, 1);
  right_layout->addLayout(find_row);
  right_layout->addWidget(buttons, 0, Qt::AlignRight);

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setChildrenCollapsible(false);
  split->setStyleSheet("QSplitter::handle { background:#1a1e24; width:2px; }");

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->addWidget(split);

  const int safe =
      std::clamp(initial_row, 0, std::max(0, count - 1));
  for (int row = 0; row < nav_->count(); ++row) {
    if (nav_->item(row)->data(Qt::UserRole).toInt() == safe) {
      nav_->setCurrentRow(row);
      break;
    }
  }
  if (nav_->currentRow() < 0 && nav_->count() > 0) {
    nav_->setCurrentRow(0);
  }
}

void HelpDialog::filter_nav(const QString &needle) {
  const QString n = needle.trimmed();
  int first_visible = -1;
  for (int row = 0; row < nav_->count(); ++row) {
    QListWidgetItem *item = nav_->item(row);
    const bool match =
        n.isEmpty() || item->text().contains(n, Qt::CaseInsensitive);
    item->setHidden(!match);
    if (match && first_visible < 0) {
      first_visible = row;
    }
  }
  if (nav_->currentItem() != nullptr && nav_->currentItem()->isHidden() &&
      first_visible >= 0) {
    nav_->setCurrentRow(first_visible);
  }
}

void HelpDialog::find_in_page() {
  if (browser_ == nullptr || page_find_ == nullptr) {
    return;
  }
  const QString text = page_find_->text();
  if (text.isEmpty()) {
    return;
  }
  if (!browser_->find(text)) {
    browser_->moveCursor(QTextCursor::Start);
    browser_->find(text);
  }
}

void HelpDialog::on_anchor(const QUrl &url) {
  if (url.scheme() != QStringLiteral("gamex")) {
    return;
  }
  QString id = url.path();
  if (id.startsWith(QLatin1Char('/'))) {
    id = id.mid(1);
  }
  if (id.isEmpty()) {
    id = url.host();
  }
  if (id.isEmpty()) {
    id = url.toString(QUrl::RemoveScheme);
    while (id.startsWith(QLatin1Char(':')) || id.startsWith(QLatin1Char('/'))) {
      id = id.mid(1);
    }
  }
  id = QUrl::fromPercentEncoding(id.toUtf8()).trimmed();

  static const struct {
    const char *link;
    const char *title;
  } kMap[] = {
      {"start", "З чого почати"},
      {"lists", "Два списки"},
      {"money", "Кейс: гроші"},
      {"bar", "Кейс: смужка життя"},
      {"sequence", "Кейс: кілька ресурсів"},
      {"freeze", "Тримати значення"},
      {"gxt", "Зберегти .gxt"},
      {"aslr", "Після перезапуску"},
      {"structure", "Структура"},
      {"keys", "Клавіші"},
      {"limits", "Межі"},
  };
  QString title = id;
  for (const auto &entry : kMap) {
    if (id == QLatin1String(entry.link)) {
      title = QString::fromUtf8(entry.title);
      break;
    }
  }

  const int index = section_index_by_title(title);
  for (int row = 0; row < nav_->count(); ++row) {
    QListWidgetItem *item = nav_->item(row);
    if (item->data(Qt::UserRole).toInt() == index) {
      if (item->isHidden() && nav_filter_ != nullptr) {
        nav_filter_->clear();
      }
      nav_->setCurrentRow(row);
      return;
    }
  }
}

void HelpDialog::show_section(int index) {
  const int count = static_cast<int>(help_section_count());
  if (index < 0 || index >= count) {
    return;
  }
  const HelpSection &section = help_sections()[index];
  browser_->setHtml(page_chrome(QString::fromUtf8(section.html)));
  browser_->moveCursor(QTextCursor::Start);
}

} // namespace gamex
