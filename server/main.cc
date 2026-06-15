#include "server/ChatServer.h"

#include "common/Config.h"

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"

namespace
{

muduo::AsyncLogging* g_asyncLog = NULL;

void asyncOutput(const char* message, int length)
{
  if (g_asyncLog != NULL)
  {
    g_asyncLog->append(message, length);
  }
}

void asyncFlush()
{
}

}  // namespace

int main()
{
  muduo::AsyncLogging asyncLog("chatservice", 500 * 1000 * 1000);
  g_asyncLog = &asyncLog;
  muduo::Logger::setOutput(asyncOutput);
  muduo::Logger::setFlush(asyncFlush);
  asyncLog.start();

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
