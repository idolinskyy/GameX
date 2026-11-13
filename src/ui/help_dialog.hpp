#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QListWidget;
class QTextBrowser;

namespace gamex {

class HelpDialog : public QDialog {
  Q_OBJECT

public:
  explicit HelpDialog(QWidget *parent = nullptr);
  // Відкрити довідку на розділі з такою назвою в навігації (точний збіг).
  explicit HelpDialog(const QString &section_title, QWidget *parent = nullptr);

private:
  void finish_setup(int initial_row);
  void show_section(int index);
  void filter_nav(const QString &needle);
  void find_in_page();
  void on_anchor(const QUrl &url);
  static QString page_chrome(const QString &body_html);
  static int section_index_by_title(const QString &title);

  QLineEdit *nav_filter_ = nullptr;
  QLineEdit *page_find_ = nullptr;
  QListWidget *nav_ = nullptr;
  QTextBrowser *browser_ = nullptr;
};

} // namespace gamex
