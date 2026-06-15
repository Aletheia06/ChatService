#include "client/ChatClient.h"

#include "common/Config.h"
#include "common/Protocol.h"

#include "muduo/base/CurrentThread.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoopThread.h"
#include "muduo/net/InetAddress.h"

#include <iostream>
#include <string>

int main()
{
  muduo::net::EventLoopThread loopThread;
  muduo::net::InetAddress serverAddr(chatservice::kServerHost,
                                     chatservice::kServerPort);
  chatservice::ChatClient client(loopThread.startLoop(), serverAddr);

  LOG_INFO << "chat_client connecting to "
           << chatservice::kServerHost << ":"
           << chatservice::kServerPort;

  client.connect();

  if (!client.waitForConnected(3.0))
  {
    std::cerr << "Failed to connect to "
              << chatservice::kServerHost << ":"
              << chatservice::kServerPort << std::endl;
    client.disconnect();
    return 1;
  }

  std::cout << "Type Stage 2 commands and press Enter. Type /quit to exit."
            << std::endl;

  std::string line;
  while (std::getline(std::cin, line))
  {
    if (line == "/quit")
    {
      break;
    }
    std::string request;
    std::string error;
    if (!chatservice::buildRequestFromCommand(line, &request, &error))
    {
      std::cout << "client: " << error << std::endl;
      continue;
    }
    client.write(request);
  }

  client.disconnect();
  client.waitForDisconnected(3.0);
  muduo::CurrentThread::sleepUsec(1000 * 1000);
  return 0;
}
