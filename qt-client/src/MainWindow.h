#ifndef CHAT_GUI_CLIENT_MAINWINDOW_H
#define CHAT_GUI_CLIENT_MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QtCore/Qt>

class ChatClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

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
  void handleConnectionError(const QString& message);
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
  QListWidgetItem* addUserIfMissing(const QString& username);
  void addRoomIfMissing(const QString& room);
  void removeRoom(const QString& room);
  void appendSystemMessage(const QString& text);
  void appendErrorMessage(const QString& text);
  void appendPrivateMessage(const QString& from, const QString& message, bool isMine);
  void appendRoomMessage(const QString& room, const QString& from, const QString& message, bool isMine);
  void appendBubble(QWidget* bubble, Qt::Alignment alignment);
  void scrollChatToBottom();
  void setConnectionStatus(const QString& text, const QString& state);
  void setActionStatus(const QString& text);
  QString selectedRoom() const;

  ChatClient* client_;
  QLabel* usernameLabel_;
  QLabel* connectionStatusLabel_;
  QListWidget* usersList_;
  QListWidget* roomsList_;
  QScrollArea* messageScrollArea_;
  QWidget* messageContainer_;
  QVBoxLayout* messageLayout_;
  QLineEdit* messageEdit_;
  QPushButton* sendButton_;
  QPushButton* refreshUsersButton_;
  QPushButton* createRoomButton_;
  QPushButton* joinRoomButton_;
  QPushButton* leaveRoomButton_;
  QLabel* chatTitleLabel_;
  QLabel* actionStatusLabel_;
  ChatMode currentMode_;
  QString currentPrivateTarget_;
  QString currentRoomName_;
  QString pendingCreateRoom_;
  QString pendingJoinRoom_;
  QString pendingLeaveRoom_;
};

#endif  // CHAT_GUI_CLIENT_MAINWINDOW_H
