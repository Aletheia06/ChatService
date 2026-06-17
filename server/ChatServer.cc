#include "server/ChatServer.h"

#include "common/Config.h"
#include "common/Protocol.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"

#include <boost/any.hpp>

#include <cstdlib>
#include <functional>
#include <stdio.h>
#include <sstream>
#include <vector>

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace chatservice
{
namespace
{

const int kDefaultHistoryLimit = 50;
const int kMaxHistoryLimit = 500;

ChatSessionPtr sessionFromConnection(
    const muduo::net::TcpConnectionPtr& connection)
{
  const ChatSessionPtr* session =
      boost::any_cast<ChatSessionPtr>(&connection->getContext());
  if (session == NULL)
  {
    return ChatSessionPtr();
  }
  return *session;
}

std::string int64ToString(int64_t value)
{
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

int64_t currentTimeSeconds()
{
  return muduo::Timestamp::now().microSecondsSinceEpoch() / 1000000;
}

int parseHistoryLimit(const std::string& value)
{
  if (value.empty())
  {
    return kDefaultHistoryLimit;
  }

  char* end = NULL;
  const long parsed = strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0)
  {
    return kDefaultHistoryLimit;
  }
  if (parsed > kMaxHistoryLimit)
  {
    return kMaxHistoryLimit;
  }
  return static_cast<int>(parsed);
}

int64_t parseBeforeTimestamp(const std::string& value)
{
  if (value.empty())
  {
    return 0;
  }

  char* end = NULL;
  const int64_t parsed = strtoll(value.c_str(), &end, 10);
  if (end == value.c_str() || parsed <= 0)
  {
    return 0;
  }
  return parsed;
}

void appendJsonStringField(std::ostringstream* stream,
                           const std::string& key,
                           const std::string& value,
                           bool* first)
{
  if (!*first)
  {
    *stream << ",";
  }
  *first = false;
  *stream << "\"" << escapeJsonString(key) << "\":"
          << "\"" << escapeJsonString(value) << "\"";
}

void appendJsonIntField(std::ostringstream* stream,
                        const std::string& key,
                        int64_t value,
                        bool* first)
{
  if (!*first)
  {
    *stream << ",";
  }
  *first = false;
  *stream << "\"" << escapeJsonString(key) << "\":" << value;
}

void appendPrivateMessageJson(std::ostringstream* stream,
                              const Message& message)
{
  *stream << "{";
  bool first = true;
  appendJsonIntField(stream, "id", message.id, &first);
  appendJsonStringField(stream, "sender", message.sender, &first);
  appendJsonStringField(stream, "receiver", message.receiver, &first);
  appendJsonStringField(stream, "content", message.content, &first);
  appendJsonIntField(stream, "created_at", message.createdAt, &first);
  *stream << "}";
}

void appendRoomMessageJson(std::ostringstream* stream,
                           const Message& message)
{
  *stream << "{";
  bool first = true;
  appendJsonIntField(stream, "id", message.id, &first);
  appendJsonStringField(stream, "sender", message.sender, &first);
  appendJsonStringField(stream, "room", message.roomId, &first);
  appendJsonStringField(stream, "content", message.content, &first);
  appendJsonIntField(stream, "created_at", message.createdAt, &first);
  *stream << "}";
}

std::string buildPrivateHistoryResponse(
    const std::string& peer,
    const std::vector<Message>& messages)
{
  std::ostringstream stream;
  stream << "{";
  bool first = true;
  appendJsonStringField(&stream, "type", "history_private_result", &first);
  appendJsonStringField(&stream, "peer", peer, &first);
  stream << ",\"messages\":[";
  for (std::size_t i = 0; i < messages.size(); ++i)
  {
    if (i != 0)
    {
      stream << ",";
    }
    appendPrivateMessageJson(&stream, messages[i]);
  }
  stream << "]}\n";
  return stream.str();
}

std::string buildRoomHistoryResponse(const std::string& room,
                                     const std::vector<Message>& messages)
{
  std::ostringstream stream;
  stream << "{";
  bool first = true;
  appendJsonStringField(&stream, "type", "history_room_result", &first);
  appendJsonStringField(&stream, "room", room, &first);
  stream << ",\"messages\":[";
  for (std::size_t i = 0; i < messages.size(); ++i)
  {
    if (i != 0)
    {
      stream << ",";
    }
    appendRoomMessageJson(&stream, messages[i]);
  }
  stream << "]}\n";
  return stream.str();
}

}  // namespace

ChatServer::ChatServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& listenAddr)
  : loop_(loop),
    server_(loop, listenAddr, kServerName),
    userManager_(),
    roomManager_(),
    storage_(),
    metrics_()
{
  server_.setThreadNum(kWorkerThreadCount);
  server_.setConnectionCallback(
      std::bind(&ChatServer::onConnection, this, _1));
  server_.setMessageCallback(
      std::bind(&ChatServer::onMessage, this, _1, _2, _3));
  loop_->runEvery(kMetricsIntervalSeconds,
                  std::bind(&ChatServer::printPerformanceStats, this));
}

bool ChatServer::initStorage(const std::string& dbPath)
{
  return storage_.init(dbPath);
}

void ChatServer::start()
{
  server_.start();
}

void ChatServer::login(const ChatSessionPtr& session,
                       const std::string& username)
{
  if (!session->username().empty())
  {
    session->sendJson(makeErrorResponse("session is already logged in"));
    return;
  }

  std::string error;
  if (!userManager_.login(username, session->connection(), &error))
  {
    session->sendJson(makeErrorResponse(error));
    return;
  }

  session->setUsername(username);
  session->sendJson(makeOkResponse("logged in as " + username));
}

void ChatServer::logout(const ChatSessionPtr& session, bool notifyClient)
{
  const std::string username = session->username();
  if (username.empty())
  {
    if (notifyClient)
    {
      session->sendJson(makeErrorResponse("session is not logged in"));
    }
    return;
  }

  roomManager_.leaveAll(username);
  userManager_.logout(username, session->connection());
  session->clearUsername();

  if (notifyClient)
  {
    session->sendJson(makeOkResponse("logged out"));
  }
}

void ChatServer::sendOnlineUsers(const ChatSessionPtr& session)
{
  JsonObject response;
  response["type"] = "users";
  response["users"] = joinList(userManager_.onlineUsers());
  session->sendJson(response);
}

void ChatServer::sendPrivate(const ChatSessionPtr& session,
                             const std::string& target,
                             const std::string& message)
{
  std::string from;
  if (!requireLoggedIn(session, &from))
  {
    return;
  }
  if (!isValidName(target))
  {
    session->sendJson(makeErrorResponse("invalid target username"));
    return;
  }
  if (message.empty())
  {
    session->sendJson(makeErrorResponse("message cannot be empty"));
    return;
  }

  const int64_t createdAt = currentTimeSeconds();
  int64_t messageId = 0;
  if (!storage_.savePrivateMessage(from,
                                   target,
                                   message,
                                   createdAt,
                                   &messageId))
  {
    session->sendJson(makeErrorResponse("failed to save private message"));
    return;
  }

  const muduo::net::TcpConnectionPtr targetConnection =
      userManager_.find(target);
  if (targetConnection)
  {
    JsonObject event;
    event["type"] = "private";
    event["from"] = from;
    event["message"] = message;
    event["id"] = int64ToString(messageId);
    event["created_at"] = int64ToString(createdAt);
    targetConnection->send(makeJsonLine(event));
    JsonObject response = makeOkResponse("private message sent");
    response["target"] = target;
    response["id"] = int64ToString(messageId);
    response["created_at"] = int64ToString(createdAt);
    session->sendJson(response);
  }
  else
  {
    JsonObject response = makeOkResponse("private message saved");
    response["target"] = target;
    response["id"] = int64ToString(messageId);
    response["created_at"] = int64ToString(createdAt);
    session->sendJson(response);
  }
}

void ChatServer::createRoom(const ChatSessionPtr& session,
                            const std::string& room)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }

  std::string error;
  if (!roomManager_.createRoom(room, &error))
  {
    session->sendJson(makeErrorResponse(error));
    return;
  }
  session->sendJson(makeOkResponse("room created"));
}

void ChatServer::joinRoom(const ChatSessionPtr& session,
                          const std::string& room)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }

  std::string error;
  if (!roomManager_.joinRoom(room, username, &error))
  {
    session->sendJson(makeErrorResponse(error));
    return;
  }
  session->sendJson(makeOkResponse("joined room"));
}

void ChatServer::leaveRoom(const ChatSessionPtr& session,
                           const std::string& room)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }

  std::string error;
  if (!roomManager_.leaveRoom(room, username, &error))
  {
    session->sendJson(makeErrorResponse(error));
    return;
  }
  session->sendJson(makeOkResponse("left room"));
}

void ChatServer::sendRoomMessage(const ChatSessionPtr& session,
                                 const std::string& room,
                                 const std::string& message)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }
  if (message.empty())
  {
    session->sendJson(makeErrorResponse("message cannot be empty"));
    return;
  }

  std::vector<std::string> members;
  std::string error;
  if (!roomManager_.membersForMessage(room, username, &members, &error))
  {
    session->sendJson(makeErrorResponse(error));
    return;
  }

  const int64_t createdAt = currentTimeSeconds();
  int64_t messageId = 0;
  if (!storage_.saveRoomMessage(room,
                                username,
                                message,
                                createdAt,
                                &messageId))
  {
    session->sendJson(makeErrorResponse("failed to save room message"));
    return;
  }

  JsonObject event;
  event["type"] = "room";
  event["room"] = room;
  event["from"] = username;
  event["message"] = message;
  event["id"] = int64ToString(messageId);
  event["created_at"] = int64ToString(createdAt);

  const std::string eventLine = makeJsonLine(event);
  const std::vector<muduo::net::TcpConnectionPtr> connections =
      userManager_.findMany(members);
  for (std::vector<muduo::net::TcpConnectionPtr>::const_iterator it =
           connections.begin();
       it != connections.end();
       ++it)
  {
    if (*it)
    {
      (*it)->send(eventLine);
    }
  }
}

void ChatServer::sendPrivateHistory(const ChatSessionPtr& session,
                                    const std::string& peer,
                                    const std::string& limitText,
                                    const std::string& beforeText)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }
  if (!isValidName(peer))
  {
    session->sendJson(makeErrorResponse("invalid peer username"));
    return;
  }

  const std::vector<Message> messages =
      storage_.loadPrivateHistory(username,
                                  peer,
                                  parseHistoryLimit(limitText),
                                  parseBeforeTimestamp(beforeText));
  session->connection()->send(buildPrivateHistoryResponse(peer, messages));
}

void ChatServer::sendRoomHistory(const ChatSessionPtr& session,
                                 const std::string& room,
                                 const std::string& limitText,
                                 const std::string& beforeText)
{
  std::string username;
  if (!requireLoggedIn(session, &username))
  {
    return;
  }
  if (!isValidName(room))
  {
    session->sendJson(makeErrorResponse("invalid room name"));
    return;
  }

  const std::vector<Message> messages =
      storage_.loadRoomHistory(room,
                               parseHistoryLimit(limitText),
                               parseBeforeTimestamp(beforeText));
  session->connection()->send(buildRoomHistoryResponse(room, messages));
}

void ChatServer::onConnection(const muduo::net::TcpConnectionPtr& connection)
{
  LOG_INFO << connection->peerAddress().toIpPort() << " -> "
           << connection->localAddress().toIpPort() << " is "
           << (connection->connected() ? "connected" : "disconnected");

  if (connection->connected())
  {
    metrics_.recordConnectionOpened();
    ChatSessionPtr session(new ChatSession(this, connection));
    connection->setContext(session);
    session->sendJson(makeOkResponse("connected"));
  }
  else
  {
    const ChatSessionPtr session = findSession(connection);
    if (session)
    {
      logout(session, false);
    }
    connection->setContext(boost::any());
    metrics_.recordConnectionDropped();
  }
}

void ChatServer::onMessage(const muduo::net::TcpConnectionPtr& connection,
                           muduo::net::Buffer* buffer,
                           muduo::Timestamp receiveTime)
{
  const ChatSessionPtr session = findSession(connection);
  if (!session)
  {
    return;
  }

  while (true)
  {
    const char* eol = buffer->findEOL();
    if (eol == NULL)
    {
      break;
    }

    std::string line(buffer->peek(), eol);
    buffer->retrieveUntil(eol + 1);
    if (!line.empty() && line[line.size() - 1] == '\r')
    {
      line.resize(line.size() - 1);
    }
    if (line.empty())
    {
      continue;
    }

    JsonObject request;
    std::string error;
    if (!parseJsonObject(line, &request, &error))
    {
      session->sendJson(makeErrorResponse("invalid json: " + error));
      continue;
    }

    LOG_INFO << connection->name() << " request at "
             << receiveTime.toString();
    metrics_.recordMessage(requestLatencyMicros(request, receiveTime));
    session->handleRequest(request);
  }

  if (buffer->readableBytes() > kMaxRequestLineBytes)
  {
    session->sendJson(makeErrorResponse("request line is too large"));
    connection->forceClose();
  }
}

ChatSessionPtr ChatServer::findSession(
    const muduo::net::TcpConnectionPtr& connection)
{
  return sessionFromConnection(connection);
}

bool ChatServer::requireLoggedIn(const ChatSessionPtr& session,
                                 std::string* username)
{
  *username = session->username();
  if (username->empty())
  {
    session->sendJson(makeErrorResponse("login required"));
    return false;
  }
  return true;
}

int64_t ChatServer::requestLatencyMicros(
    const JsonObject& request,
    muduo::Timestamp receiveTime) const
{
  const int64_t nowMicros = muduo::Timestamp::now().microSecondsSinceEpoch();
  JsonObject::const_iterator it = request.find("sent_at_us");
  if (it != request.end())
  {
    char* end = NULL;
    const int64_t sentMicros = strtoll(it->second.c_str(), &end, 10);
    if (end != it->second.c_str() && sentMicros > 0 &&
        nowMicros >= sentMicros)
    {
      return nowMicros - sentMicros;
    }
  }

  return nowMicros - receiveTime.microSecondsSinceEpoch();
}

void ChatServer::printPerformanceStats()
{
  const MetricsSnapshot snapshot =
      metrics_.snapshotAndReset(userManager_.onlineCount(),
                                kMetricsIntervalSeconds);

  std::ostringstream stream;
  stream << "stats online_users=" << snapshot.onlineUsers
         << " messages_per_second=" << snapshot.messagesPerSecond
         << " average_latency_us=" << snapshot.averageLatencyMicros
         << " total_messages=" << snapshot.totalMessages
         << " total_connections=" << snapshot.totalConnections
         << " dropped_connections=" << snapshot.droppedConnections;

  const std::string line = stream.str();
  LOG_INFO << line;
  printf("%s\n", line.c_str());
  fflush(stdout);
}

}  // namespace chatservice
