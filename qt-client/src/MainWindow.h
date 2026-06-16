#ifndef CHAT_GUI_CLIENT_MAINWINDOW_H
#define CHAT_GUI_CLIENT_MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>

class ChatClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextBrowser;

class MainWindow : public QMainWindow
{
  Q_OBJECT

 public:
  explicit MainWindow(ChatClient* client, QWidget* parent = nullptr);

 private slots:
  void refreshUsers();
  void createRoom();
  void joinRoom();
  void leaveRoom();
  void sendCurrentMessage();
  void selectPrivateChat();
  void selectRoomChat();
  void updateUsers(const QStringList& users);
  void showInfo(const QString& message);
  void showError(const QString& message);
  void handlePrivateMessage(const QString& from, const QString& message);
  void handleRoomMessage(const QString& room, const QString& from, const QString& message);
  void handleDisconnected();

 private:
  enum class ChatMode
  {
    None,
    Private,
    Room
  };

  void buildUi();
  void connectSignals();
  void setCurrentChat(ChatMode mode, const QString& name);
  void addRoomIfMissing(const QString& room);
  void removeRoom(const QString& room);
  void appendHistory(const QString& text);
  QString selectedRoom() const;

  ChatClient* client_;
  QListWidget* usersList_;
  QListWidget* roomsList_;
  QTextBrowser* chatHistory_;
  QLineEdit* messageEdit_;
  QPushButton* sendButton_;
  QPushButton* refreshUsersButton_;
  QPushButton* createRoomButton_;
  QPushButton* joinRoomButton_;
  QPushButton* leaveRoomButton_;
  QLabel* chatTitleLabel_;
  QLabel* statusLabel_;
  ChatMode currentMode_;
  QString currentTarget_;
  QString pendingCreateRoom_;
  QString pendingJoinRoom_;
  QString pendingLeaveRoom_;
};

#endif  // CHAT_GUI_CLIENT_MAINWINDOW_H
