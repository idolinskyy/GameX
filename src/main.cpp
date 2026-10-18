#include "ui/main_window.hpp"

#include "gamex_build_info.hpp"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("GameX");
  QApplication::setApplicationVersion(QStringLiteral(GAMEX_VERSION_STRING));
  QApplication::setOrganizationName("gamex");
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/gamex.png")));

  gamex::MainWindow window;
  window.setWindowIcon(QApplication::windowIcon());
  window.show();
  return app.exec();
}
