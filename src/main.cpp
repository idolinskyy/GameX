#include "ui/main_window.hpp"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("GameX");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setOrganizationName("gamex");
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/gamex.png")));

  gamex::MainWindow window;
  window.setWindowIcon(QApplication::windowIcon());
  window.show();
  return app.exec();
}
