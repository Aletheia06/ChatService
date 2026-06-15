#ifndef CHATSERVICE_SERVER_ROOMMANAGER_H
#define CHATSERVICE_SERVER_ROOMMANAGER_H

#include "muduo/base/Mutex.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chatservice
{

class RoomManager
{
 public:
  bool createRoom(const std::string& room, std::string* error);
  bool joinRoom(const std::string& room,
                const std::string& username,
                std::string* error);
  bool leaveRoom(const std::string& room,
                 const std::string& username,
                 std::string* error);
  void leaveAll(const std::string& username);
  bool membersForMessage(const std::string& room,
                         const std::string& username,
                         std::vector<std::string>* members,
                         std::string* error) const;

 private:
  mutable muduo::MutexLock mutex_;
  std::map<std::string, std::set<std::string> > rooms_ GUARDED_BY(mutex_);
  std::map<std::string, std::set<std::string> > userRooms_ GUARDED_BY(mutex_);
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_ROOMMANAGER_H
