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

std::vector<muduo::net::TcpConnectionPtr> UserManager::findMany(
    const std::vector<std::string>& usernames) const
{
  std::vector<muduo::net::TcpConnectionPtr> connections;
  connections.reserve(usernames.size());

  muduo::MutexLockGuard lock(mutex_);
  for (std::vector<std::string>::const_iterator it = usernames.begin();
       it != usernames.end();
       ++it)
  {
    const std::map<std::string, muduo::net::TcpConnectionPtr>::const_iterator
        userIt = users_.find(*it);
    if (userIt != users_.end())
    {
      connections.push_back(userIt->second);
    }
  }

  return connections;
}

int64_t UserManager::onlineCount() const
{
  muduo::MutexLockGuard lock(mutex_);
  return static_cast<int64_t>(users_.size());
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
