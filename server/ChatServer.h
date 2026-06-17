#ifndef CHATSERVICE_SERVER_CHATSERVER_H
#define CHATSERVICE_SERVER_CHATSERVER_H

#include "server/ChatSession.h"
#include "server/RoomManager.h"
#include "server/ServerMetrics.h"
#include "server/ChatStorage.h"
#include "server/UserManager.h"

#include "muduo/net/TcpServer.h"

#include <string>

namespace chatservice
{

class ChatServer
{
 public:
  ChatServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr);

  bool initStorage(const std::string& dbPath);
  void start();

  void login(const ChatSessionPtr& session, const std::string& username);
  void logout(const ChatSessionPtr& session, bool notifyClient);
  void sendOnlineUsers(const ChatSessionPtr& session);
  void sendPrivate(const ChatSessionPtr& session,
                   const std::string& target,
                   const std::string& message);
  void createRoom(const ChatSessionPtr& session, const std::string& room);
  void joinRoom(const ChatSessionPtr& session, const std::string& room);
  void leaveRoom(const ChatSessionPtr& session, const std::string& room);
  void sendRoomMessage(const ChatSessionPtr& session,
                       const std::string& room,
                       const std::string& message);
  void sendPrivateHistory(const ChatSessionPtr& session,
                          const std::string& peer,
                          const std::string& limitText,
                          const std::string& beforeText);
  void sendRoomHistory(const ChatSessionPtr& session,
                       const std::string& room,
                       const std::string& limitText,
                       const std::string& beforeText);

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& connection);
  void onMessage(const muduo::net::TcpConnectionPtr& connection,
                 muduo::net::Buffer* buffer,
                 muduo::Timestamp receiveTime);

  ChatSessionPtr findSession(const muduo::net::TcpConnectionPtr& connection);
  bool requireLoggedIn(const ChatSessionPtr& session, std::string* username);
  int64_t requestLatencyMicros(const JsonObject& request,
                               muduo::Timestamp receiveTime) const;
  void printPerformanceStats();

  muduo::net::EventLoop* loop_;
  muduo::net::TcpServer server_;
  UserManager userManager_;
  RoomManager roomManager_;
  ChatStorage storage_;
  ServerMetrics metrics_;
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_CHATSERVER_H
