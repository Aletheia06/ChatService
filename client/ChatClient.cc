#include "client/ChatClient.h"

#include "common/Config.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <functional>
#include <iostream>

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace chatservice
{

ChatClient::ChatClient(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& serverAddr)
  : client_(loop, serverAddr, kClientName),
    condition_(mutex_),
    connected_(false)
{
  client_.setConnectionCallback(
      std::bind(&ChatClient::onConnection, this, _1));
  client_.setMessageCallback(
      std::bind(&ChatClient::onMessage, this, _1, _2, _3));
}

void ChatClient::connect()
{
  client_.connect();
}

void ChatClient::disconnect()
{
  client_.disconnect();
}

bool ChatClient::waitForConnected(double seconds)
{
  muduo::MutexLockGuard lock(mutex_);
  while (!connected_)
  {
    if (condition_.waitForSeconds(seconds))
    {
      return connected_;
    }
  }
  return true;
}

bool ChatClient::waitForDisconnected(double seconds)
{
  muduo::MutexLockGuard lock(mutex_);
  while (connected_)
  {
    if (condition_.waitForSeconds(seconds))
    {
      return !connected_;
    }
  }
  return true;
}

void ChatClient::write(const std::string& message)
{
  muduo::MutexLockGuard lock(mutex_);
  if (connection_)
  {
    connection_->send(message);
  }
  else
  {
    std::cout << "Not connected to server yet." << std::endl;
  }
}

void ChatClient::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
  LOG_INFO << conn->localAddress().toIpPort() << " -> "
           << conn->peerAddress().toIpPort() << " is "
           << (conn->connected() ? "connected" : "disconnected");

  muduo::MutexLockGuard lock(mutex_);
  if (conn->connected())
  {
    connection_ = conn;
    connected_ = true;
    condition_.notifyAll();
    std::cout << "Connected to " << conn->peerAddress().toIpPort()
              << std::endl;
  }
  else
  {
    connection_.reset();
    connected_ = false;
    condition_.notifyAll();
    std::cout << "Disconnected from server." << std::endl;
  }
}

void ChatClient::onMessage(const muduo::net::TcpConnectionPtr&,
                           muduo::net::Buffer* buffer,
                           muduo::Timestamp)
{
  while (true)
  {
    const char* eol = buffer->findEOL();
    if (eol == NULL)
    {
      break;
    }

    muduo::string message(buffer->peek(), eol);
    buffer->retrieveUntil(eol + 1);
    if (!message.empty() && message[message.size() - 1] == '\r')
    {
      message.resize(message.size() - 1);
    }
    std::cout << "server: " << message << std::endl;
  }
}

}  // namespace chatservice
