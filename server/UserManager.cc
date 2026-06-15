#include "server/UserManager.h"

#include "common/Protocol.h"

namespace chatservice
{

bool UserManager::login(const std::string& username,
                        const muduo::net::TcpConnectionPtr& connection,
                        std::string* error)
{
  if (!isValidName(username))
  {
    *error = "invalid username";
    return false;
  }

  muduo::MutexLockGuard lock(mutex_);
  if (users_.find(username) != users_.end())
  {
    *error = "username is already online";
    return false;
  }

  users_[username] = connection;
  return true;
}

void UserManager::logout(const std::string& username,
                         const muduo::net::TcpConnectionPtr& connection)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, muduo::net::TcpConnectionPtr>::iterator it =
      users_.find(username);
  if (it != users_.end() && it->second == connection)
  {
    users_.erase(it);
  }
}

muduo::net::TcpConnectionPtr UserManager::find(
    const std::string& username) const
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, muduo::net::TcpConnectionPtr>::const_iterator it =
      users_.find(username);
  if (it == users_.end())
  {
    return muduo::net::TcpConnectionPtr();
  }
  return it->second;
}

std::vector<std::string> UserManager::onlineUsers() const
{
  std::vector<std::string> users;
  muduo::MutexLockGuard lock(mutex_);
  for (std::map<std::string, muduo::net::TcpConnectionPtr>::const_iterator it =
           users_.begin();
       it != users_.end();
       ++it)
  {
    users.push_back(it->first);
  }
  return users;
}

}  // namespace chatservice
