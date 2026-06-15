#ifndef CHATSERVICE_CLIENT_CHATCLIENT_H
#define CHATSERVICE_CLIENT_CHATCLIENT_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/TcpClient.h"

#include <string>

namespace chatservice
{

class ChatClient
{
 public:
  ChatClient(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& serverAddr);

  void connect();
  void disconnect();
  bool waitForConnected(double seconds);
  bool waitForDisconnected(double seconds);
  void write(const std::string& message);

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);
  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buffer,
                 muduo::Timestamp receiveTime);

  muduo::net::TcpClient client_;
  muduo::MutexLock mutex_;
  muduo::Condition condition_ GUARDED_BY(mutex_);
  muduo::net::TcpConnectionPtr connection_ GUARDED_BY(mutex_);
  bool connected_ GUARDED_BY(mutex_);
};

}  // namespace chatservice

#endif  // CHATSERVICE_CLIENT_CHATCLIENT_H
