#ifndef CHAT_GUI_CLIENT_PROTOCOL_H
#define CHAT_GUI_CLIENT_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

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

QJsonObject parseJsonLine(const QByteArray& line, QString* errorMessage = nullptr);
QString stringValue(const QJsonObject& object, const QString& key);

}  // namespace Protocol

#endif  // CHAT_GUI_CLIENT_PROTOCOL_H
