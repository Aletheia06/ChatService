#ifndef CHAT_GUI_CLIENT_LOGINWINDOW_H
#define CHAT_GUI_CLIENT_LOGINWINDOW_H

#include <QString>
#include <QWidget>

class ChatClient;
class QLabel;
class QLineEdit;
class MainWindow;
class QPushButton;
class QSpinBox;

class LoginWindow : public QWidget
{
  Q_OBJECT

 public:
  explicit LoginWindow(QWidget* parent = nullptr);
  ~LoginWindow() override;

 private slots:
  void onLoginClicked();
  void onSocketConnected();
  void onLoginSucceeded(const QString& username);
  void onLoginFailed(const QString& message);
  void onConnectionError(const QString& message);
  void onDisconnected();
  void setStatus(const QString& message);

 private:
  void buildUi();
  void setLoginEnabled(bool enabled);

  QLineEdit* hostEdit_;
  QSpinBox* portSpinBox_;
  QLineEdit* usernameEdit_;
  QPushButton* loginButton_;
  QLabel* statusLabel_;
  ChatClient* client_;
  MainWindow* mainWindow_;
  QString pendingUsername_;
};

#endif  // CHAT_GUI_CLIENT_LOGINWINDOW_H
