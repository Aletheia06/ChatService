#ifndef CHATSERVICE_SERVER_USERMANAGER_H
#define CHATSERVICE_SERVER_USERMANAGER_H

#include "muduo/base/Mutex.h"
#include "muduo/net/TcpConnection.h"

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

namespace chatservice
{

class UserManager
{
 public:
  bool login(const std::string& username,
             const muduo::net::TcpConnectionPtr& connection,
             std::string* error);
  void logout(const std::string& username,
              const muduo::net::TcpConnectionPtr& connection);
  muduo::net::TcpConnectionPtr find(const std::string& username) const;
  std::vector<muduo::net::TcpConnectionPtr> findMany(
      const std::vector<std::string>& usernames) const;
  int64_t onlineCount() const;
  std::vector<std::string> onlineUsers() const;

 private:
  mutable muduo::MutexLock mutex_;
  std::map<std::string, muduo::net::TcpConnectionPtr> users_ GUARDED_BY(mutex_);
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_USERMANAGER_H
