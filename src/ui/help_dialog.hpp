#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QTextBrowser;

namespace gamex {

class HelpDialog : public QDialog {
  Q_OBJECT

public:
  explicit HelpDialog(QWidget *parent = nullptr);

private:
  void show_section(int index);
  static QString page_chrome(const QString &body_html);

  QListWidget *nav_ = nullptr;
  QTextBrowser *browser_ = nullptr;
};

} // namespace gamex
