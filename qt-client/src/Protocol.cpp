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

QByteArray buildPrivateHistoryRequest(const QString& peer, int limit)
{
  QJsonObject object = baseObject("history_private");
  object.insert("peer", peer);
  object.insert("limit", limit);
  return buildLine(object);
}

QByteArray buildRoomHistoryRequest(const QString& room, int limit)
{
  QJsonObject object = baseObject("history_room");
  object.insert("room", room);
  object.insert("limit", limit);
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

qint64 int64Value(const QJsonObject& object, const QString& key, qint64 fallback)
{
  const QJsonValue value = object.value(key);
  if (value.isDouble())
  {
    return static_cast<qint64>(value.toDouble());
  }

  if (value.isString())
  {
    bool ok = false;
    const qint64 parsed = value.toString().toLongLong(&ok);
    return ok ? parsed : fallback;
  }

  return fallback;
}

}  // namespace Protocol
