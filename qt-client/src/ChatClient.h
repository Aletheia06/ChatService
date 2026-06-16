#ifndef CHAT_GUI_CLIENT_CHATCLIENT_H
#define CHAT_GUI_CLIENT_CHATCLIENT_H

#include <QAbstractSocket>
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>
#include <QString>
#include <QStringList>

class ChatClient : public QObject
{
  Q_OBJECT

 public:
  explicit ChatClient(QObject* parent = nullptr);

  QString username() const;
  bool isLoggedIn() const;

 public slots:
  void connectToServer(const QString& host, quint16 port);
  void disconnectFromServer();
  void login(const QString& username);
  void logout();
  void requestUsers();
  void sendPrivateMessage(const QString& target, const QString& message);
  void createRoom(const QString& room);
  void joinRoom(const QString& room);
  void leaveRoom(const QString& room);
  void sendRoomMessage(const QString& room, const QString& message);

 signals:
  void connected();
  void disconnected();
  void connectionError(const QString& message);
  void loginSucceeded(const QString& username);
  void loginFailed(const QString& message);
  void infoMessage(const QString& message);
  void errorMessage(const QString& message);
  void usersUpdated(const QStringList& users);
  void privateMessageReceived(const QString& from, const QString& message);
  void roomMessageReceived(const QString& room, const QString& from, const QString& message);

 private slots:
  void onConnected();
  void onDisconnected();
  void onReadyRead();
  void onSocketError(QAbstractSocket::SocketError error);

 private:
  void sendLine(const QByteArray& line);
  void handleLine(const QByteArray& line);
  void handleObject(const QJsonObject& object);
  void handleUsers(const QString& users);
  void resetLoginState();

  QTcpSocket* socket_;
  QByteArray buffer_;
  QString pendingLoginUsername_;
  QString currentUsername_;
  bool loggedIn_;
};

#endif  // CHAT_GUI_CLIENT_CHATCLIENT_H
