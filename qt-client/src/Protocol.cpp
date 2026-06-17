#include "Protocol.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace
{

QByteArray buildLine(const QJsonObject& object)
{
  QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
  line.append('\n');
  return line;
}

QJsonObject baseObject(const QString& type)
{
  QJsonObject object;
  object.insert("type", type);
  return object;
}

}  // namespace

namespace Protocol
{

QByteArray buildLogin(const QString& username)
{
  QJsonObject object = baseObject("login");
  object.insert("username", username);
  return buildLine(object);
}

QByteArray buildLogout()
{
  return buildLine(baseObject("logout"));
}

QByteArray buildUsersRequest()
{
  return buildLine(baseObject("users"));
}

QByteArray buildPrivateMessage(const QString& target, const QString& message)
{
  QJsonObject object = baseObject("private");
  object.insert("target", target);
  object.insert("message", message);
  const QByteArray line = buildLine(object);
  qDebug() << "Private message JSON line:" << line
           << "endsWithNewline:" << line.endsWith('\n');
  return line;
}

QByteArray buildCreateRoom(const QString& room)
{
  QJsonObject object = baseObject("create_room");
  object.insert("room", room);
  return buildLine(object);
}

QByteArray buildJoinRoom(const QString& room)
{
  QJsonObject object = baseObject("join");
  object.insert("room", room);
  return buildLine(object);
}

QByteArray buildLeaveRoom(const QString& room)
{
  QJsonObject object = baseObject("leave");
  object.insert("room", room);
  return buildLine(object);
}

QByteArray buildRoomMessage(const QString& room, const QString& message)
{
  QJsonObject object = baseObject("room_msg");
  object.insert("room", room);
  object.insert("message", message);
  return buildLine(object);
}

QJsonObject parseJsonLine(const QByteArray& line, QString* errorMessage)
{
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
  if (error.error != QJsonParseError::NoError)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = error.errorString();
    }
    return QJsonObject();
  }

  if (!document.isObject())
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "JSON line is not an object";
    }
    return QJsonObject();
  }

  if (errorMessage != nullptr)
  {
    errorMessage->clear();
  }
  return document.object();
}

QString stringValue(const QJsonObject& object, const QString& key)
{
  const QJsonValue value = object.value(key);
  return value.isString() ? value.toString() : QString();
}

}  // namespace Protocol
