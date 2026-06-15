#ifndef CHATSERVICE_COMMON_CONFIG_H
#define CHATSERVICE_COMMON_CONFIG_H

#include <stdint.h>

namespace chatservice
{

constexpr const char* kServerHost = "127.0.0.1";
constexpr uint16_t kServerPort = 8888;
constexpr const char* kServerName = "ChatEchoServer";
constexpr const char* kClientName = "ChatClient";

}  // namespace chatservice

#endif  // CHATSERVICE_COMMON_CONFIG_H
