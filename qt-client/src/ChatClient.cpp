#include "ChatClient.h"

#include "Protocol.h"

#include <QJsonObject>

ChatClient::ChatClient(QObject* parent)
  : QObject(parent),
    socket_(new QTcpSocket(this)),
    loggedIn_(false)
{
  connect(socket_, &QTcpSocket::connected, this, &ChatClient::onConnected);
  connect(socket_, &QTcpSocket::disconnected, this, &ChatClient::onDisconnected);
  connect(socket_, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
  connect(socket_, &QTcpSocket::errorOccurred, this, &ChatClient::onSocketError);
}

QString ChatClient::username() const
{
  return currentUsername_;
}

bool ChatClient::isLoggedIn() const
{
  return loggedIn_;
}

void ChatClient::connectToServer(const QString& host, quint16 port)
{
  buffer_.clear();
  resetLoginState();

  if (socket_->state() != QAbstractSocket::UnconnectedState)
  {
    socket_->abort();
  }

  socket_->connectToHost(host, port);
}

void ChatClient::disconnectFromServer()
{
  if (socket_->state() == QAbstractSocket::UnconnectedState)
  {
    return;
  }
  socket_->disconnectFromHost();
}

void ChatClient::login(const QString& username)
{
  pendingLoginUsername_ = username;
  sendLine(Protocol::buildLogin(username));
}

void ChatClient::logout()
{
  sendLine(Protocol::buildLogout());
}

void ChatClient::requestUsers()
{
  sendLine(Protocol::buildUsersRequest());
}

void ChatClient::sendPrivateMessage(const QString& target, const QString& message)
{
  sendLine(Protocol::buildPrivateMessage(target, message));
}

void ChatClient::createRoom(const QString& room)
{
  sendLine(Protocol::buildCreateRoom(room));
}

void ChatClient::joinRoom(const QString& room)
{
  sendLine(Protocol::buildJoinRoom(room));
}

void ChatClient::leaveRoom(const QString& room)
{
  sendLine(Protocol::buildLeaveRoom(room));
}

void ChatClient::sendRoomMessage(const QString& room, const QString& message)
{
  sendLine(Protocol::buildRoomMessage(room, message));
}

void ChatClient::onConnected()
{
  emit connected();
}

void ChatClient::onDisconnected()
{
  resetLoginState();
  buffer_.clear();
  emit disconnected();
}

void ChatClient::onReadyRead()
{
  buffer_.append(socket_->readAll());

  while (true)
  {
    const qsizetype newline = buffer_.indexOf('\n');
    if (newline < 0)
    {
      break;
    }

    const QByteArray line = buffer_.left(newline);
    buffer_.remove(0, newline + 1);
    if (!line.trimmed().isEmpty())
    {
      handleLine(line);
    }
  }
}

void ChatClient::onSocketError(QAbstractSocket::SocketError)
{
  emit connectionError(socket_->errorString());
  if (!pendingLoginUsername_.isEmpty() && !loggedIn_)
  {
    emit loginFailed(socket_->errorString());
  }
}

void ChatClient::sendLine(const QByteArray& line)
{
  if (socket_->state() != QAbstractSocket::ConnectedState)
  {
    emit connectionError("Socket is not connected");
    return;
  }

  socket_->write(line);
}

void ChatClient::handleLine(const QByteArray& line)
{
  QString error;
  const QJsonObject object = Protocol::parseJsonLine(line, &error);
  if (!error.isEmpty())
  {
    emit errorMessage("Invalid JSON from server: " + error);
    return;
  }

  handleObject(object);
}

void ChatClient::handleObject(const QJsonObject& object)
{
  const QString type = Protocol::stringValue(object, "type");

  if (type == "ok")
  {
    const QString message = Protocol::stringValue(object, "message");
    if (message.startsWith("logged in as "))
    {
      currentUsername_ = message.mid(QString("logged in as ").size());
      pendingLoginUsername_.clear();
      loggedIn_ = true;
      emit loginSucceeded(currentUsername_);
      return;
    }

    if (message == "logged out")
    {
      resetLoginState();
    }

    emit infoMessage(message);
    return;
  }

  if (type == "error")
  {
    const QString message = Protocol::stringValue(object, "message");
    if (!pendingLoginUsername_.isEmpty() && !loggedIn_)
    {
      pendingLoginUsername_.clear();
      emit loginFailed(message);
    }
    emit errorMessage(message);
    return;
  }

  if (type == "users")
  {
    handleUsers(Protocol::stringValue(object, "users"));
    return;
  }

  if (type == "private")
  {
    emit privateMessageReceived(Protocol::stringValue(object, "from"),
                                Protocol::stringValue(object, "message"));
    return;
  }

  if (type == "room")
  {
    emit roomMessageReceived(Protocol::stringValue(object, "room"),
                             Protocol::stringValue(object, "from"),
                             Protocol::stringValue(object, "message"));
    return;
  }

  emit errorMessage("Unknown server message type: " + type);
}

void ChatClient::handleUsers(const QString& users)
{
  QStringList result;
  const QStringList parts = users.split(',', Qt::SkipEmptyParts);
  for (const QString& part : parts)
  {
    const QString user = part.trimmed();
    if (!user.isEmpty() && user != currentUsername_)
    {
      result.append(user);
    }
  }

  emit usersUpdated(result);
}

void ChatClient::resetLoginState()
{
  pendingLoginUsername_.clear();
  currentUsername_.clear();
  loggedIn_ = false;
}
