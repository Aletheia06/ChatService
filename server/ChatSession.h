#ifndef CHATSERVICE_SERVER_CHATSESSION_H
#define CHATSERVICE_SERVER_CHATSESSION_H

#include "common/Json.h"

#include "muduo/base/Mutex.h"
#include "muduo/net/TcpConnection.h"

#include <memory>
#include <string>

namespace chatservice
{

class ChatServer;

class ChatSession : public std::enable_shared_from_this<ChatSession>
{
 public:
  ChatSession(ChatServer* server,
              const muduo::net::TcpConnectionPtr& connection);

  void handleRequest(const JsonObject& request);
  void sendJson(const JsonObject& object);

  const muduo::net::TcpConnectionPtr& connection() const;
  std::string username() const;
  void setUsername(const std::string& username);
  void clearUsername();

 private:
  ChatServer* server_;
  muduo::net::TcpConnectionPtr connection_;
  mutable muduo::MutexLock mutex_;
  std::string username_ GUARDED_BY(mutex_);
};

typedef std::shared_ptr<ChatSession> ChatSessionPtr;

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_CHATSESSION_H
