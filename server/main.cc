#include "server/ChatServer.h"
#include "server/ChatStorage.h"

#include "common/Config.h"

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

muduo::AsyncLogging* g_asyncLog = NULL;

struct ServerOptions
{
  ServerOptions()
    : dbPath(chatservice::kDefaultDatabasePath),
      dumpHistory(false)
  {
  }

  std::string dbPath;
  std::string dumpPath;
  bool dumpHistory;
};

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

void printUsage(const char* program)
{
  std::cerr << "Usage: " << program
            << " [--db PATH] [--dump-history OUTPUT_JSON]\n";
}

bool parseOptions(int argc,
                  char* argv[],
                  ServerOptions* options,
                  std::string* error)
{
  const char* envDbPath = std::getenv("CHATSERVICE_DB_PATH");
  if (envDbPath != NULL && envDbPath[0] != '\0')
  {
    options->dbPath = envDbPath;
  }

  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h")
    {
      printUsage(argv[0]);
      return false;
    }
    if (arg == "--db")
    {
      if (i + 1 >= argc)
      {
        *error = "--db requires a path";
        return false;
      }
      options->dbPath = argv[++i];
      continue;
    }
    if (arg == "--dump-history")
    {
      if (i + 1 >= argc)
      {
        *error = "--dump-history requires an output path";
        return false;
      }
      options->dumpHistory = true;
      options->dumpPath = argv[++i];
      continue;
    }

    *error = "unknown argument: " + arg;
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char* argv[])
{
  ServerOptions options;
  std::string optionError;
  if (!parseOptions(argc, argv, &options, &optionError))
  {
    if (!optionError.empty())
    {
      std::cerr << optionError << std::endl;
      printUsage(argv[0]);
      return 1;
    }
    return 0;
  }

  if (options.dumpHistory)
  {
    chatservice::ChatStorage storage;
    if (!storage.init(options.dbPath))
    {
      std::cerr << "Failed to open database: " << options.dbPath << std::endl;
      return 1;
    }

    std::string dumpError;
    if (!storage.dumpAllMessagesJson(options.dumpPath, &dumpError))
    {
      std::cerr << "Failed to dump history: " << dumpError << std::endl;
      return 1;
    }

    std::cout << "History dumped to " << options.dumpPath << std::endl;
    return 0;
  }

  muduo::AsyncLogging asyncLog("chatservice", 500 * 1000 * 1000);
  g_asyncLog = &asyncLog;
  muduo::Logger::setOutput(asyncOutput);
  muduo::Logger::setFlush(asyncFlush);
  asyncLog.start();

  muduo::net::EventLoop loop;
  muduo::net::InetAddress listenAddr(chatservice::kServerHost,
                                     chatservice::kServerPort);
  chatservice::ChatServer server(&loop, listenAddr);
  if (!server.initStorage(options.dbPath))
  {
    std::cerr << "Failed to initialize SQLite database: "
              << options.dbPath << std::endl;
    return 1;
  }

  LOG_INFO << "chat_server listening on "
           << chatservice::kServerHost << ":"
           << chatservice::kServerPort;
  LOG_INFO << "chat_server SQLite database " << options.dbPath;

  server.start();
  loop.loop();
  return 0;
}
