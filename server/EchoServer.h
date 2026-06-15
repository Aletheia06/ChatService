#ifndef CHATSERVICE_SERVER_ECHOSERVER_H
#define CHATSERVICE_SERVER_ECHOSERVER_H

#include "muduo/net/TcpServer.h"

namespace chatservice
{

class EchoServer
{
 public:
  EchoServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr);

  void start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);
  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buffer,
                 muduo::Timestamp receiveTime);

  muduo::net::TcpServer server_;
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_ECHOSERVER_H
