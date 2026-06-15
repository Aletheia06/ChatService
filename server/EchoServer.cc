#include "server/EchoServer.h"

#include "common/Config.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <functional>

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace chatservice
{

EchoServer::EchoServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& listenAddr)
  : server_(loop, listenAddr, kServerName)
{
  server_.setConnectionCallback(
      std::bind(&EchoServer::onConnection, this, _1));
  server_.setMessageCallback(
      std::bind(&EchoServer::onMessage, this, _1, _2, _3));
}

void EchoServer::start()
{
  server_.start();
}

void EchoServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
  LOG_INFO << conn->peerAddress().toIpPort() << " -> "
           << conn->localAddress().toIpPort() << " is "
           << (conn->connected() ? "connected" : "disconnected");
}

void EchoServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buffer,
                           muduo::Timestamp receiveTime)
{
  muduo::string message = buffer->retrieveAllAsString();
  LOG_INFO << conn->name() << " received " << message.size()
           << " bytes at " << receiveTime.toString();
  conn->send(message);
}

}  // namespace chatservice
