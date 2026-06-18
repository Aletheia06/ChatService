#include "server/ChatSession.h"

#include "common/Protocol.h"
#include "server/ChatServer.h"

namespace chatservice
{
namespace
{

std::string fieldOrEmpty(const JsonObject& object, const std::string& key)
{
  const JsonObject::const_iterator it = object.find(key);
  if (it == object.end())
  {
    return std::string();
  }
  return it->second;
}

}  // namespace

ChatSession::ChatSession(ChatServer* server,
                         const muduo::net::TcpConnectionPtr& connection)
  : server_(server),
    connection_(connection)
{
}

void ChatSession::handleRequest(const JsonObject& request)
{
  const std::string type = fieldOrEmpty(request, "type");

  if (type == "login")
  {
    server_->login(shared_from_this(), fieldOrEmpty(request, "username"));
  }
  else if (type == "logout")
  {
    server_->logout(shared_from_this(), true);
  }
  else if (type == "users")
  {
    server_->sendOnlineUsers(shared_from_this());
  }
  else if (type == "private")
  {
    server_->sendPrivate(shared_from_this(),
                         fieldOrEmpty(request, "target"),
                         fieldOrEmpty(request, "message"));
  }
  else if (type == "create_room")
  {
    server_->createRoom(shared_from_this(), fieldOrEmpty(request, "room"));
  }
  else if (type == "join")
  {
    server_->joinRoom(shared_from_this(), fieldOrEmpty(request, "room"));
  }
  else if (type == "leave")
  {
    server_->leaveRoom(shared_from_this(), fieldOrEmpty(request, "room"));
  }
  else if (type == "room_msg")
  {
    server_->sendRoomMessage(shared_from_this(),
                             fieldOrEmpty(request, "room"),
                             fieldOrEmpty(request, "message"));
  }
  else if (type == "history_private")
  {
    std::string before = fieldOrEmpty(request, "before");
    if (before.empty())
    {
      before = fieldOrEmpty(request, "before_timestamp");
    }
    if (before.empty())
    {
      before = fieldOrEmpty(request, "beforeTimestamp");
    }
    server_->sendPrivateHistory(shared_from_this(),
                                fieldOrEmpty(request, "peer"),
                                fieldOrEmpty(request, "limit"),
                                before);
  }
  else if (type == "history_room")
  {
    std::string before = fieldOrEmpty(request, "before");
    if (before.empty())
    {
      before = fieldOrEmpty(request, "before_timestamp");
    }
    if (before.empty())
    {
      before = fieldOrEmpty(request, "beforeTimestamp");
    }
    server_->sendRoomHistory(shared_from_this(),
                             fieldOrEmpty(request, "room"),
                             fieldOrEmpty(request, "limit"),
                             before);
  }
  else if (type == "call_invite" ||
           type == "call_accept" ||
           type == "call_reject" ||
           type == "call_cancel" ||
           type == "call_hangup" ||
           type == "call_timeout" ||
           type == "webrtc_offer" ||
           type == "webrtc_answer" ||
           type == "ice_candidate")
  {
    server_->handleCallRequest(shared_from_this(), request);
  }
  else
  {
    sendJson(makeErrorResponse("unknown request type"));
  }
}

void ChatSession::sendJson(const JsonObject& object)
{
  connection_->send(makeJsonLine(object));
}

const muduo::net::TcpConnectionPtr& ChatSession::connection() const
{
  return connection_;
}

std::string ChatSession::username() const
{
  muduo::MutexLockGuard lock(mutex_);
  return username_;
}

void ChatSession::setUsername(const std::string& username)
{
  muduo::MutexLockGuard lock(mutex_);
  username_ = username;
}

void ChatSession::clearUsername()
{
  muduo::MutexLockGuard lock(mutex_);
  username_.clear();
}

}  // namespace chatservice
