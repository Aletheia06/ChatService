#include "server/RoomManager.h"

#include "common/Protocol.h"

namespace chatservice
{

bool RoomManager::createRoom(const std::string& room, std::string* error)
{
  if (!isValidName(room))
  {
    *error = "invalid room name";
    return false;
  }

  muduo::MutexLockGuard lock(mutex_);
  if (rooms_.find(room) != rooms_.end())
  {
    *error = "room already exists";
    return false;
  }

  rooms_[room] = std::set<std::string>();
  return true;
}

bool RoomManager::joinRoom(const std::string& room,
                           const std::string& username,
                           std::string* error)
{
  muduo::MutexLockGuard lock(mutex_);
  std::map<std::string, std::set<std::string> >::iterator roomIt =
      rooms_.find(room);
  if (roomIt == rooms_.end())
  {
    *error = "room does not exist";
    return false;
  }

  roomIt->second.insert(username);
  userRooms_[username].insert(room);
  return true;
}

bool RoomManager::leaveRoom(const std::string& room,
                            const std::string& username,
                            std::string* error)
{
  muduo::MutexLockGuard lock(mutex_);
  std::map<std::string, std::set<std::string> >::iterator roomIt =
      rooms_.find(room);
  if (roomIt == rooms_.end())
  {
    *error = "room does not exist";
    return false;
  }

  if (roomIt->second.erase(username) == 0)
  {
    *error = "user is not in room";
    return false;
  }

  std::map<std::string, std::set<std::string> >::iterator userIt =
      userRooms_.find(username);
  if (userIt != userRooms_.end())
  {
    userIt->second.erase(room);
    if (userIt->second.empty())
    {
      userRooms_.erase(userIt);
    }
  }

  return true;
}

void RoomManager::leaveAll(const std::string& username)
{
  muduo::MutexLockGuard lock(mutex_);
  std::map<std::string, std::set<std::string> >::iterator userIt =
      userRooms_.find(username);
  if (userIt == userRooms_.end())
  {
    return;
  }

  const std::set<std::string> rooms = userIt->second;
  for (std::set<std::string>::const_iterator it = rooms.begin();
       it != rooms.end();
       ++it)
  {
    std::map<std::string, std::set<std::string> >::iterator roomIt =
        rooms_.find(*it);
    if (roomIt != rooms_.end())
    {
      roomIt->second.erase(username);
    }
  }

  userRooms_.erase(userIt);
}

int64_t RoomManager::roomCount() const
{
  muduo::MutexLockGuard lock(mutex_);
  int64_t activeRooms = 0;
  for (std::map<std::string, std::set<std::string> >::const_iterator it =
           rooms_.begin();
       it != rooms_.end();
       ++it)
  {
    if (!it->second.empty())
    {
      ++activeRooms;
    }
  }
  return activeRooms;
}

bool RoomManager::membersForMessage(const std::string& room,
                                    const std::string& username,
                                    std::vector<std::string>* members,
                                    std::string* error) const
{
  members->clear();
  muduo::MutexLockGuard lock(mutex_);
  std::map<std::string, std::set<std::string> >::const_iterator roomIt =
      rooms_.find(room);
  if (roomIt == rooms_.end())
  {
    *error = "room does not exist";
    return false;
  }

  if (roomIt->second.find(username) == roomIt->second.end())
  {
    *error = "user is not in room";
    return false;
  }

  for (std::set<std::string>::const_iterator it = roomIt->second.begin();
       it != roomIt->second.end();
       ++it)
  {
    members->push_back(*it);
  }
  return true;
}

}  // namespace chatservice
