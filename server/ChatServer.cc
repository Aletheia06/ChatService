#include "server/ChatServer.h"

#include "common/Config.h"
#include "common/Protocol.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <functional>
#include <vector>

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace chatservice
{

ChatServer::ChatServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& listenAddr)
  : server_(loop, listenAddr, kServerName)
{
  server_.setConnectionCallback(
      std::bind(&ChatServer::onConnection, this, _1));
  server_.setMessageCallback(
      std::bind(&ChatServer::onMessage, this, _1, _2, _3));
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
  for (std::vector<std::string>::const_iterator it = members.begin();
       it != members.end();
       ++it)
  {
    const muduo::net::TcpConnectionPtr connection = userManager_.find(*it);
    if (connection)
    {
      connection->send(eventLine);
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
    ChatSessionPtr session(new ChatSession(this, connection));
    {
      muduo::MutexLockGuard lock(mutex_);
      sessions_[connection->name()] = session;
    }
    session->sendJson(makeOkResponse("connected"));
  }
  else
  {
    const ChatSessionPtr session = findSession(connection);
    if (session)
    {
      logout(session, false);
      removeSession(connection);
    }
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
    session->handleRequest(request);
  }
}

ChatSessionPtr ChatServer::findSession(
    const muduo::net::TcpConnectionPtr& connection)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, ChatSessionPtr>::const_iterator it =
      sessions_.find(connection->name());
  if (it == sessions_.end())
  {
    return ChatSessionPtr();
  }
  return it->second;
}

void ChatServer::removeSession(
    const muduo::net::TcpConnectionPtr& connection)
{
  muduo::MutexLockGuard lock(mutex_);
  sessions_.erase(connection->name());
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

}  // namespace chatservice
