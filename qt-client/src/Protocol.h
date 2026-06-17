#ifndef CHAT_GUI_CLIENT_PROTOCOL_H
#define CHAT_GUI_CLIENT_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace Protocol
{

QByteArray buildLogin(const QString& username);
QByteArray buildLogout();
QByteArray buildUsersRequest();
QByteArray buildPrivateMessage(const QString& target, const QString& message);
QByteArray buildCreateRoom(const QString& room);
QByteArray buildJoinRoom(const QString& room);
QByteArray buildLeaveRoom(const QString& room);
QByteArray buildRoomMessage(const QString& room, const QString& message);
QByteArray buildPrivateHistoryRequest(const QString& peer, int limit);
QByteArray buildRoomHistoryRequest(const QString& room, int limit);

QJsonObject parseJsonLine(const QByteArray& line, QString* errorMessage = nullptr);
QString stringValue(const QJsonObject& object, const QString& key);
qint64 int64Value(const QJsonObject& object, const QString& key, qint64 fallback = 0);

}  // namespace Protocol

#endif  // CHAT_GUI_CLIENT_PROTOCOL_H
