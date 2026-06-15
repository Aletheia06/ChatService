#include "server/ChatServer.h"

#include "common/Config.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"

int main()
{
  muduo::net::EventLoop loop;
  muduo::net::InetAddress listenAddr(chatservice::kServerHost,
                                     chatservice::kServerPort);
  chatservice::ChatServer server(&loop, listenAddr);

  LOG_INFO << "chat_server listening on "
           << chatservice::kServerHost << ":"
           << chatservice::kServerPort;

  server.start();
  loop.loop();
  return 0;
}
