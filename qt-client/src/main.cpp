#include "LoginWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QApplication::setApplicationName("Chat GUI Client");

  LoginWindow loginWindow;
  loginWindow.show();

  return app.exec();
}
