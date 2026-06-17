#ifndef CHATSERVICE_COMMON_CONFIG_H
#define CHATSERVICE_COMMON_CONFIG_H

#include <stddef.h>
#include <stdint.h>

namespace chatservice
{

constexpr const char* kServerHost = "127.0.0.1";
constexpr uint16_t kServerPort = 8888;
constexpr const char* kServerName = "ChatServer";
constexpr const char* kClientName = "ChatClient";
constexpr const char* kDefaultDatabasePath = "chat_history.sqlite3";
constexpr int kWorkerThreadCount = 4;
constexpr double kMetricsIntervalSeconds = 5.0;
constexpr size_t kMaxRequestLineBytes = 64 * 1024;

}  // namespace chatservice

#endif  // CHATSERVICE_COMMON_CONFIG_H
