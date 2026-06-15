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

}  // namespace

ChatServer::ChatServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& listenAddr)
  : loop_(loop),
    server_(loop, listenAddr, kServerName),
    userManager_(),
    roomManager_(),
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

  const muduo::net::TcpConnectionPtr targetConnection =
      userManager_.find(target);
  if (!targetConnection)
  {
    session->sendJson(makeErrorResponse("target user is not online"));
    return;
  }

  JsonObject event;
  event["type"] = "private";
  event["from"] = from;
  event["message"] = message;
  targetConnection->send(makeJsonLine(event));
  session->sendJson(makeOkResponse("private message sent"));
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

  JsonObject event;
  event["type"] = "room";
  event["room"] = room;
  event["from"] = username;
  event["message"] = message;

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
