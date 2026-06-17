#include "AppStyle.h"
#include "LoginWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QApplication::setApplicationName("Chat GUI Client");
  app.setStyleSheet(buildApplicationStyleSheet());

  LoginWindow loginWindow;
  loginWindow.show();

  return app.exec();
}
